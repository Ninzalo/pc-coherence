// RW-H-012 (H-MEDIA): in-box Media Foundation H.264 encoder backend.
//
// Design notes (measured against SDK 10.0.26100 on the dev PC - see the
// header and docs/STATUS.md):
//  * Candidate selection BY BEHAVIOR: start() probes each in-box H.264
//    encoder MFT (activate -> unlock -> negotiate -> bounded 2-frame
//    smoke encode) and selects the first that actually PRODUCES output
//    in this process/session. Measured: the NVIDIA H.264 Encoder MFT
//    produces valid annex-B in an interactive session; Intel QSV
//    activates everywhere but produces no output on a machine whose
//    iGPU has no display; "Microsoft AVC" (DX12) cannot negotiate.
//    A failed start is therefore honest (the pipeline reports
//    "start_failed") instead of a silent zero-byte stream.
//  * Output layout (pinned to the merged client decode pipeline,
//    remote-windows-client/Sources/RemoteWindowsApp/Video/VideoPipeline.swift:
//    10-16, 131-166, 226, 277-282): annex-B access unit per EncodedFrame;
//    SPS+PPS in-band in the first keyframe access unit; 3- and 4-byte
//    start codes both accepted client-side; the client converts to AVCC
//    itself. No host-side re-framing: the framing layer
//    (media/media_framing.hpp:16-21) carries the payload opaque.
//  * On-demand keyframe: MFT_MESSAGE_COMMAND_FLUSH (mftransform.h, SDK
//    26100) + best-effort CODECAPI_AVEncVideoForceKeyFrame attribute.
//    After a mid-stream flush the MFT restarts the stream, so the next
//    encoded frame is an IDR with fresh in-band SPS/PPS.
//  * No detached threads (RW-H-013 lesson): all MFT calls run on the
//    calling thread; the only foreign entry point is the MFT's async
//    callback, which only touches a critical-section-guarded slot.
//
// Win32/MSVC only - NOT RUN on macOS by design (AGENTS.md).

#include "win32/video/h264_encoder_mf.hpp"

#include <mferror.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mftransform.h>
#include <d3d11.h>
#include <codecapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <utility>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace remotewindows::win32 {
namespace {

using core::CapturedFrame;
using core::EncodedFrame;

constexpr HRESULT kMfNotAccepting = static_cast<HRESULT>(0xC00D36B5);

// Internal sentinel: "bounded pump timed out, no output sample". NOT an
// MFT HRESULT (MF_E_NOTFOUND was removed from mferror.h in SDK 26100;
// MF_E_NOT_FOUND 0xC00D36D5 is a different, unrelated status).
constexpr HRESULT kNoSample = static_cast<HRESULT>(0x00E00001);

DWORD now_ms() {
  return static_cast<DWORD>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

// Pack a (num, den) ratio into the SDK 26100 MF_MT_FRAME_RATE format:
// packed UINT64 (num << 32 | den). The classic MFVideoFrameRate{N,D}
// GUIDs are ABSENT from SDK 26100 headers; the probe phase negotiated
// successfully with this packed form (verified on both working MFTs).
UINT64 pack_rate(UINT32 num, UINT32 den) {
  return (static_cast<UINT64>(num) << 32) | den;
}

UINT32 clamp_fps(UINT32 fps) {
  if (fps == 0) return 30;
  return std::min<UINT32>(std::max<UINT32>(fps, 1), 120);
}

// Bitrate heuristic: ~0.04 bpp, clamped to [200k, 8M] bps.
UINT32 bitrate_heuristic(uint32_t w, uint32_t h, uint32_t fps) {
  const uint64_t bps = static_cast<uint64_t>(w) * h * fps * 4 / 100;
  return static_cast<UINT32>(
      std::min<uint64_t>(std::max<uint64_t>(bps, 200000), 8000000));
}

// Annex-B scan: does the access unit contain an IDR slice (NAL type 5)?
// Accepts 3- and 4-byte start codes (the client accepts both too).
bool has_idr_nal(const uint8_t* p, size_t n) {
  for (size_t i = 0; i + 4 <= n; i++) {
    if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1 &&
        (p[i + 3] & 0x1F) == 5) {
      return true;
    }
    if (i + 4 < n && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 &&
        p[i + 3] == 1 && (p[i + 4] & 0x1F) == 5) {
      return true;
    }
  }
  return false;
}

// Does the access unit carry SPS(7) and PPS(8) in-band? The client
// requires both in the FIRST keyframe (VideoPipeline.swift:159-166).
bool has_sps_pps_nals(const uint8_t* p, size_t n) {
  bool sps = false, pps = false;
  for (size_t i = 0; i + 4 <= n && !(sps && pps); i++) {
    if (p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1) {
      const uint32_t t = p[i + 3] & 0x1F;
      if (t == 7) sps = true;
      if (t == 8) pps = true;
    } else if (i + 4 < n && p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 0 &&
               p[i + 3] == 1) {
      const uint32_t t = p[i + 4] & 0x1F;
      if (t == 7) sps = true;
      if (t == 8) pps = true;
    }
  }
  return sps && pps;
}

// BT.601 limited-range BGRA -> NV12 (the MFT's input type).
std::vector<uint8_t> bgra_to_nv12(const CapturedFrame& f) {
  const uint32_t w = f.width, h = f.height;
  std::vector<uint8_t> nv12(static_cast<size_t>(w) * h * 3 / 2);
  const uint8_t* p = f.pixels.data();
  uint8_t* y = nv12.data();
  uint8_t* c = y + static_cast<size_t>(w) * h;
  for (uint32_t py = 0; py < h; py++) {
    for (uint32_t px = 0; px < w; px++) {
      const uint32_t B = p[0], G = p[1], R = p[2];
      p += 4;
      y[px] = static_cast<uint8_t>(
          std::min<uint32_t>(235u,
                             std::max<uint32_t>(16u,
                                                16u + (66 * R + 129 * G +
                                                       25 * B) >>
                                                           8)));
      if ((py & 1u) == 0 && (px & 1u) == 0) {
        const int r = static_cast<int>(R), g = static_cast<int>(G),
                  b = static_cast<int>(B);
        c[0] = static_cast<uint8_t>(
            std::min<uint32_t>(255u,
                               std::max<uint32_t>(0u,
                                                  128u + (uint32_t)((-38 * r -
                                                                      74 * g +
                                                                      112 * b) >>
                                                                    8))));
        c[1] = static_cast<uint8_t>(
            std::min<uint32_t>(255u,
                               std::max<uint32_t>(
                                   0u,
                                   128u + (uint32_t)((112 * r - 94 * g -
                                                      18 * b) >>
                                                    8))));
        c += 2;
      }
    }
    y += w;
  }
  return nv12;
}

// Build one NV12 input sample; returns an added ref on success.
HRESULT make_nv12_sample(uint32_t w, uint32_t h, uint32_t fps, uint64_t seq,
                         const std::vector<uint8_t>& nv12,
                         IMFSample** out) {
  IMFMediaBuffer* buf = nullptr;
  if (FAILED(MFCreateMemoryBuffer(static_cast<DWORD>(nv12.size()), &buf)))
    return E_OUTOFMEMORY;
  BYTE* pp = nullptr;
  DWORD ml = 0, cl = 0;
  if (SUCCEEDED(buf->Lock(&pp, &ml, &cl)) && pp) {
    memcpy(pp, nv12.data(), nv12.size());
    buf->Unlock();
  } else {
    buf->Release();
    return E_FAIL;
  }
  buf->SetCurrentLength(static_cast<DWORD>(nv12.size()));
  IMFSample* sample = nullptr;
  if (FAILED(MFCreateSample(&sample))) {
    buf->Release();
    return E_OUTOFMEMORY;
  }
  sample->AddBuffer(buf);
  buf->Release();
  sample->SetSampleTime(static_cast<LONGLONG>(10000000 / fps) *
                        static_cast<LONGLONG>(seq));
  sample->SetSampleDuration(static_cast<LONGLONG>(10000000 / fps));
  *out = sample;
  return S_OK;
}

// Copy a contiguous sample's bytes into a vector (for NAL scanning /
// delivery). Returns false if the sample is empty or not contiguous.
bool sample_bytes(IMFSample* sample, std::vector<uint8_t>* out) {
  DWORD len = 0;
  if (FAILED(sample->GetTotalLength(&len)) || len == 0) return false;
  out->resize(len);
  IMFMediaBuffer* rb = nullptr;
  if (FAILED(sample->ConvertToContiguousBuffer(&rb)) || !rb) return false;
  BYTE* d = nullptr;
  DWORD ml = 0, cl = 0;
  bool ok = false;
  if (SUCCEEDED(rb->Lock(&d, &ml, &cl)) && d) {
    memcpy(out->data(), d, std::min<DWORD>(len, cl));
    rb->Unlock();
    ok = true;
  }
  rb->Release();
  return ok;
}

// Pull ONE ready output sample through the bounded event loop, WITHOUT
// delivering it (smoke phase). Measured MFT behavior: ProcessOutput
// returns S_OK with pSample==NULL (NO_SAMPLE) when the MFT is not
// ready — that is not an error; the next event will say. Ticking or
// re-calling ProcessOutput in that state corrupts the MFT.
HRESULT pull_one_output(IMFTransform* t, IMFMediaEventGenerator* gen,
                        EventSink* sink, DWORD deadline_ms,
                        IMFSample** out_sample) {
  *out_sample = nullptr;
  while (now_ms() < deadline_ms) {
    DWORD wr = WaitForSingleObject(sink->have_event(), 25);
    if (wr != WAIT_OBJECT_0) break;  // bounded timeout
    IMFAsyncResult* ar = sink->take_pending();
    if (!ar) continue;
    IMFMediaEvent* ev = nullptr;
    HRESULT he = gen->EndGetEvent(ar, &ev);
    ar->Release();
    if (FAILED(he)) return he;
    if (!ev) continue;
    DWORD met = 0;
    ev->GetType(&met);
    HRESULT status = S_OK;
    ev->GetStatus(&status);
    if (met == static_cast<DWORD>(METransformHaveOutput)) {
      IMFSample* oSample = nullptr;
      MFCreateSample(&oSample);
      IMFMediaBuffer* oBuf = nullptr;
      MFCreateMemoryBuffer(8 * 1024 * 1024, &oBuf);
      oSample->AddBuffer(oBuf);
      oBuf->Release();
      MFT_OUTPUT_DATA_BUFFER odb;
      memset(&odb, 0, sizeof(odb));
      odb.dwStreamID = 0;
      odb.pSample = oSample;
      DWORD ost = 0;
      HRESULT ho = t->ProcessOutput(0, 1, &odb, &ost);
      if (SUCCEEDED(ho) && odb.pSample) {
        IMFSample* rs = odb.pSample;
        if (rs != oSample) oSample->Release();
        ev->Release();
        *out_sample = rs;
        return S_OK;
      }
      oSample->Release();  // S_OK + NO_SAMPLE: keep waiting
    } else if (met == static_cast<DWORD>(MEError)) {
      ev->Release();
      return (status == S_OK) ? E_FAIL : status;
    }
    // METransformNeedInput etc.: nothing to feed from here.
    ev->Release();
    ResetEvent(sink->have_event());
    if (FAILED(gen->BeginGetEvent(sink, nullptr))) break;
  }
  return kNoSample;  // bounded timeout, no sample
}

}  // namespace

// --------------------------------------------------------------- EventSink

EventSink::EventSink() {
  have_ev_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset
  InitializeCriticalSection(&cs_);
}

EventSink::~EventSink() {
  for (IMFAsyncResult* ar : pending_) {
    if (ar) ar->Release();
  }
  DeleteCriticalSection(&cs_);
  if (have_ev_) CloseHandle(have_ev_);
}

HRESULT EventSink::QueryInterface(REFIID riid, void** pp) {
  if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFAsyncCallback)) {
    *pp = static_cast<IUnknown*>(this);
    AddRef();
    return S_OK;
  }
  *pp = nullptr;
  return E_NOINTERFACE;
}

ULONG EventSink::AddRef() {
  return static_cast<ULONG>(InterlockedIncrement(&ref_));
}

ULONG EventSink::Release() {
  LONG r = InterlockedDecrement(&ref_);
  if (r == 0) delete this;
  return static_cast<ULONG>(r);
}

// SDK 26100 vtable (mfobjects.h): GetParameters + Invoke.
HRESULT EventSink::GetParameters(DWORD* flags, DWORD* queue) {
  if (flags) *flags = 0;
  if (queue) *queue = MFASYNC_CALLBACK_QUEUE_STANDARD;
  return S_OK;
}

HRESULT EventSink::Invoke(IMFAsyncResult* ar) {
  {
    EnterCriticalSection(&cs_);
    if (ar) {
      ar->AddRef();  // hand a ref to the pump thread
      pending_.push_back(ar);
    }
  }
  LeaveCriticalSection(&cs_);
  if (have_ev_) SetEvent(have_ev_);
  return S_OK;
}

HANDLE EventSink::have_event() const { return have_ev_; }

IMFAsyncResult* EventSink::take_pending() {
  EnterCriticalSection(&cs_);
  IMFAsyncResult* ar = pending_.empty() ? nullptr : pending_.front();
  if (ar) pending_.erase(pending_.begin());
  LeaveCriticalSection(&cs_);
  return ar;
}

// ------------------------------------------------------------- MfH264Encoder

MfH264Encoder::MfH264Encoder(std::string window_id,
                             core::IEncodedFrameConsumer& sink)
    : window_id_(std::move(window_id)), sink_(sink) {}

MfH264Encoder::~MfH264Encoder() { stop(); }

void MfH264Encoder::releaseOwned() {
  // Consume any events still queued so the MFT holds no pending
  // callback operations before we release it.
  if (event_sink_ && gen_) {
    for (int guard = 0; guard < 64; guard++) {
      IMFAsyncResult* ar = event_sink_->take_pending();
      if (!ar) break;
      IMFMediaEvent* ev = nullptr;
      if (SUCCEEDED(gen_->EndGetEvent(ar, &ev))) {
        if (ev) ev->Release();
      }
      ar->Release();
    }
  }
  if (mft_) {
    mft_->Release();
    mft_ = nullptr;
  }
  if (gen_) {
    gen_->Release();
    gen_ = nullptr;  // may be the MFT's last external ref; the MFT then
                     // releases the callback ref it holds
  }
  if (event_sink_) {
    event_sink_->Release();  // our ref; self-deletes at refcount 0
    event_sink_ = nullptr;
  }
  if (mft_attrs_) {
    mft_attrs_->Release();
    mft_attrs_ = nullptr;
  }
  if (out_type_) {
    out_type_->Release();
    out_type_ = nullptr;
  }
  if (in_type_) {
    in_type_->Release();
    in_type_ = nullptr;
  }
  if (d3d_mgr_) {
    d3d_mgr_->Release();
    d3d_mgr_ = nullptr;
  }
  if (d3d11_) {
    d3d11_->Release();
    d3d11_ = nullptr;
  }
  for (auto& q : input_queue_) {
    q.sample->Release();
  }
  input_queue_.clear();
  input_seqs_.clear();
  flush_pending_ = false;
  streaming_ = false;
}

void MfH264Encoder::stop() {
  if (!started_) return;
  if (!dead_ && mft_) {
    // End of stream + bounded drain (EOF -> DrainComplete).
    mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    pumpUntil(now_ms() + 2000);
  }
  releaseOwned();
  started_ = false;
  if (com_inited_here_ && GetCurrentThreadId() == com_thread_id_) {
    CoUninitialize();
    com_inited_here_ = false;
  }
}

bool MfH264Encoder::start(uint32_t width, uint32_t height,
                          uint32_t fps_hint) {
  if (started_) return true;
  if (width == 0 || height == 0) return false;
  width_ = width;
  height_ = height;
  fps_ = clamp_fps(fps_hint);

  if (!com_inited_here_) {
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
      com_inited_here_ = true;
      com_thread_id_ = GetCurrentThreadId();
    }
    // Else: COM already initialized on this thread (any mode is fine —
    // the async MFT event model needs no message pumping).
  }

  MFT_REGISTER_TYPE_INFO filter;
  memset(&filter, 0, sizeof(filter));
  filter.guidMajorType = MFMediaType_Video;
  filter.guidSubtype = MFVideoFormat_H264;
  IMFActivate** arr = nullptr;
  UINT32 count = 0;
  HRESULT he = MFTEnum2(MFT_CATEGORY_VIDEO_ENCODER,
                        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT |
                            MFT_ENUM_FLAG_HARDWARE,
                        nullptr, &filter, nullptr, &arr, &count);
  if (FAILED(he) || !arr) return false;

  // Order by measured preference: NVIDIA H.264 first (works in an
  // interactive session on the dev PC, no D3D manager needed), then
  // Intel QSV. Measured non-functional candidates (the sync MSH264
  // shim: 0 bytes across 9 probe variants; "Microsoft AVC" DX12:
  // template output not negotiable in SDK 26100) are skipped by name —
  // the behavior smoke below would reject them anyway, but skipping
  // keeps start() fast.
  struct Cand {
    IMFActivate* act;
    std::string name;
  };
  std::vector<Cand> order;
  for (UINT32 i = 0; i < count; i++) {
    LPWSTR nm = nullptr;
    UINT32 nlen = 0;
    if (FAILED(arr[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute, &nm,
                                          &nlen)) ||
        !nm) {
      continue;
    }
    std::string name(nlen, '\0');
    for (UINT32 k = 0; k < nlen; k++) name[k] = static_cast<char>(nm[k]);
    CoTaskMemFree(nm);
    if (name.find("NVIDIA H.264") == std::string::npos &&
        name.find("Quick Sync") == std::string::npos) {
      continue;
    }
    order.push_back({arr[i], std::move(name)});
  }
  std::stable_sort(order.begin(), order.end(),
                   [](const Cand& a, const Cand& b) {
                     const bool na =
                         a.name.find("NVIDIA H.264") != std::string::npos;
                     const bool nb =
                         b.name.find("NVIDIA H.264") != std::string::npos;
                     return na != nb ? na : false;
                   });

  bool ok = false;
  for (auto& c : order) {
    std::string detail;
    if (tryCandidate(c.act, c.name, width, height, fps_, &detail)) {
      selected_ = c.name;
      ok = true;
      break;
    }
  }

  // ActivateObject hands us a new object; the activate objects from
  // the enumeration are ours to release in ALL cases (the selected
  // MFT's lifetime is independent of its activate object).
  for (auto& c : order) {
    c.act->Release();
  }
  CoTaskMemFree(arr);

  if (!ok) {
    releaseOwned();  // defensive: nothing should be running
    return false;
  }
  started_ = true;
  return true;
}

bool MfH264Encoder::tryCandidate(IMFActivate* act, const std::string& name,
                                 uint32_t w, uint32_t h, uint32_t fps,
                                 std::string* detail) {
  (void)name;
  IMFTransform* t = nullptr;
  HRESULT ha = act->ActivateObject(IID_PPV_ARGS(&t));
  if (FAILED(ha)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "activate=%08X", static_cast<unsigned>(ha));
    *detail = buf;
    return false;
  }

  // Async MFTs require the unlock attribute (verified: both working
  // candidates are MF_TRANSFORM_ASYNC and refuse negotiation without
  // MF_TRANSFORM_ASYNC_UNLOCK=1).
  IMFAttributes* attrs = nullptr;
  t->GetAttributes(&attrs);
  if (attrs) {
    attrs->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, 1);
  }

  // D3D11 manager: best effort. The NVIDIA MFT encoded fine WITHOUT
  // one (measured); the QSV MFT accepted one but produced no output
  // anyway — a failure here is not fatal.
  IMFDXGIDeviceManager* mgr = nullptr;
  ID3D11Device* dev = nullptr;
  {
    D3D_FEATURE_LEVEL fl[2] = {D3D_FEATURE_LEVEL_11_1,
                               D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL used = D3D_FEATURE_LEVEL_11_0;  // out-param; SDK 26100
    // removed the D3D_FEATURE_LEVEL_UNKNOWN enumerator
    ID3D11DeviceContext* ctx = nullptr;
    if (SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE,
                                    nullptr, 0, fl, 2, D3D11_SDK_VERSION,
                                    &dev, &used, &ctx))) {
      UINT rt = 0;
      if (SUCCEEDED(MFCreateDXGIDeviceManager(&rt, &mgr)) && mgr) {
        mgr->ResetDevice(dev, rt);
      } else {
        dev->Release();
        dev = nullptr;
      }
      if (ctx) ctx->Release();
    } else {
      dev = nullptr;
    }
  }
  if (mgr) {
    HRESULT hs = t->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                   reinterpret_cast<ULONG_PTR>(mgr));
    if (FAILED(hs)) {
      mgr->Release();
      dev->Release();
      mgr = nullptr;
      dev = nullptr;
    }
  }

  auto fail = [t, attrs, mgr, dev](std::string* d, const char* msg,
                                   HRESULT hr) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s=%08X", msg, static_cast<unsigned>(hr));
    *d = buf;
    if (attrs) attrs->Release();
    if (mgr) mgr->Release();
    if (dev) dev->Release();
    t->Release();
  };

  // --- Output type: the advertised (possibly template) type, filled --
  IMFMediaType* out = nullptr;
  HRESULT ho0 = t->GetOutputAvailableType(0, 0, &out);
  if (FAILED(ho0) || !out) {
    fail(detail, "negotiate_failed", ho0);
    return false;
  }
  UINT64 fs = 0, fr = 0;
  if (FAILED(out->GetUINT64(MF_MT_FRAME_SIZE, &fs)) ||
      (fs & 0xFFFFFFFF) == 0) {
    out->SetUINT64(MF_MT_FRAME_SIZE, (static_cast<UINT64>(w) << 32) | h);
  } else {
    // Advertised a concrete size; ask for ours (measured: both working
    // MFTs accept a custom-built type at the requested size).
    out->SetUINT64(MF_MT_FRAME_SIZE, (static_cast<UINT64>(w) << 32) | h);
  }
  if (FAILED(out->GetUINT64(MF_MT_FRAME_RATE, &fr)) || (fr & 0xFFFFFFFF) == 0) {
    out->SetUINT64(MF_MT_FRAME_RATE, pack_rate(fps, 1));
  }
  UINT32 u32 = 0;
  if (FAILED(out->GetUINT32(MF_MT_DEFAULT_STRIDE, &u32)) || u32 == 0) {
    out->SetUINT32(MF_MT_DEFAULT_STRIDE, w);
  }
  if (FAILED(out->GetUINT32(MF_MT_AVG_BITRATE, &u32)) || u32 == 0) {
    out->SetUINT32(MF_MT_AVG_BITRATE, bitrate_heuristic(w, h, fps));
  }
  out->SetUINT32(MF_MT_INTERLACE_MODE,
                 static_cast<UINT32>(MFVideoInterlace_Progressive));
  out->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, 1);
  HRESULT hos = t->SetOutputType(0, out, 0);

  // --- Input type: first advertised NV12, filled ---------------------
  IMFMediaType* in = nullptr;
  HRESULT his = E_FAIL;
  if (SUCCEEDED(hos)) {
    for (DWORD i = 0;; i++) {
      IMFMediaType* cand = nullptr;
      HRESULT h = t->GetInputAvailableType(0, i, &cand);
      if (h == MF_E_NO_MORE_TYPES || FAILED(h)) break;
      GUID sub;
      cand->GetGUID(MF_MT_SUBTYPE, &sub);
      if (IsEqualGUID(sub, MFVideoFormat_NV12)) {
        UINT64 cfs = 0, cfr = 0;
        if (FAILED(cand->GetUINT64(MF_MT_FRAME_SIZE, &cfs)) ||
            (cfs & 0xFFFFFFFF) == 0) {
          cand->SetUINT64(MF_MT_FRAME_SIZE,
                          (static_cast<UINT64>(w) << 32) | h);
        }
        if (FAILED(cand->GetUINT64(MF_MT_FRAME_RATE, &cfr)) ||
            (cfr & 0xFFFFFFFF) == 0) {
          cand->SetUINT64(MF_MT_FRAME_RATE, pack_rate(fps, 1));
        }
        UINT32 s32 = 0;
        if (FAILED(cand->GetUINT32(MF_MT_DEFAULT_STRIDE, &s32)) || s32 == 0) {
          cand->SetUINT32(MF_MT_DEFAULT_STRIDE, w);
        }
        if (FAILED(cand->GetUINT32(MF_MT_AVG_BITRATE, &s32)) || s32 == 0) {
          cand->SetUINT32(MF_MT_AVG_BITRATE, bitrate_heuristic(w, h, fps));
        }
        cand->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, 1);
        his = t->SetInputType(0, cand, 0);
        if (SUCCEEDED(his)) in = cand;
        cand->Release();
        if (SUCCEEDED(his)) break;
      } else {
        cand->Release();
      }
    }
  }
  if (FAILED(his) || !in) {
    fail(detail, "negotiate_failed", his);
    out->Release();
    return false;
  }

  // --- Event generator + start of stream -----------------------------
  IMFMediaEventGenerator* gen = nullptr;
  HRESULT hq = t->QueryInterface(IID_PPV_ARGS(&gen));
  if (FAILED(hq) || !gen) {
    fail(detail, "negotiate_failed", hq);
    out->Release();
    in->Release();
    return false;
  }

  EventSink* sink = new EventSink();  // ref 1 (ours); the MFT takes its own
  if (FAILED(gen->BeginGetEvent(sink, nullptr))) {
    sink->Release();
    fail(detail, "negotiate_failed", E_FAIL);
    gen->Release();
    out->Release();
    in->Release();
    return false;
  }
  HRESULT hsos = t->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
  if (FAILED(hsos)) {
    sink->Release();
    fail(detail, "negotiate_failed", hsos);
    gen->Release();
    out->Release();
    in->Release();
    return false;
  }

  // --- Smoke encode: 2 frames, bounded 2 s, expect SPS+PPS+IDR --------
  // The measured NVIDIA MFT emits exactly this in the first access
  // unit. A candidate that negotiates but never produces output (e.g.
  // QSV in a headless session on the dev PC) fails here and is
  // rejected — so start() is honest about the environment instead of
  // delivering a silent zero-byte stream.
  const DWORD plane = w * h;
  const DWORD cb = plane + plane / 2;
  std::vector<uint8_t> nv12(cb);
  for (DWORD f = 0; f < 2; f++) {
    for (DWORD y = 0; y < h; y++) {
      for (DWORD x = 0; x < w; x++) {
        nv12[y * w + x] = static_cast<uint8_t>((x + y + f * 32) & 0xFF);
      }
    }
    for (DWORD i = 0; i < plane / 2; i++) {
      nv12[plane + i] = static_cast<uint8_t>(
          0x80 + static_cast<int>((i + f * 16) % 32) - 16);
    }
    IMFSample* s = nullptr;
    if (FAILED(make_nv12_sample(w, h, fps, f, nv12, &s))) {
      sink->Release();
      fail(detail, "negotiate_failed", E_OUTOFMEMORY);
      gen->Release();
      out->Release();
      in->Release();
      return false;
    }
    HRESULT hp = t->ProcessInput(0, s, 0);
    if (FAILED(hp)) {
      if (hp == kMfNotAccepting) {
        // Queue full: pump until the MFT is ready, retry (bounded).
        DWORD d = now_ms() + 500;
        bool fed = false;
        while (now_ms() < d) {
          IMFSample* dummy = nullptr;
          (void)pull_one_output(t, gen, sink, now_ms() + 50, &dummy);
          if (dummy) dummy->Release();
          if (SUCCEEDED(t->ProcessInput(0, s, 0))) {
            fed = true;
            break;
          }
        }
        if (!fed) {
          s->Release();
          sink->Release();
          fail(detail, "negotiate_failed", kMfNotAccepting);
          gen->Release();
          out->Release();
          in->Release();
          return false;
        }
      } else {
        s->Release();
        sink->Release();
        fail(detail, "negotiate_failed", hp);
        gen->Release();
        out->Release();
        in->Release();
        return false;
      }
    }
    s->Release();
  }

  // Wait for the smoke output; the first access unit must carry
  // SPS+PPS+IDR (the layout the client requires of the first
  // keyframe).
  {
    bool got = false;
    DWORD deadline = now_ms() + 2000;
    while (!got && now_ms() < deadline) {
      IMFSample* o = nullptr;
      HRESULT hr = pull_one_output(t, gen, sink, now_ms() + 100, &o);
      if (hr == kNoSample) break;  // bounded timeout
      if (FAILED(hr)) {
        sink->Release();
        fail(detail, "negotiate_failed", hr);
        gen->Release();
        out->Release();
        in->Release();
        return false;
      }
      std::vector<uint8_t> bytes;
      if (sample_bytes(o, &bytes)) {
        got = has_sps_pps_nals(bytes.data(), bytes.size()) &&
              has_idr_nal(bytes.data(), bytes.size());
      }
      o->Release();
    }
    if (!got) {
      sink->Release();
      *detail = "no_output_in_smoke";
      gen->Release();
      out->Release();
      in->Release();
      if (attrs) attrs->Release();
      if (mgr) mgr->Release();
      if (dev) dev->Release();
      t->Release();
      return false;
    }
    // Drain the remaining smoke output so the MFT is in a clean state.
    {
      DWORD d2 = now_ms() + 500;
      while (now_ms() < d2) {
        IMFSample* o = nullptr;
        HRESULT hr = pull_one_output(t, gen, sink, now_ms() + 50, &o);
        if (hr != S_OK) break;
        o->Release();
      }
    }
    // Tear the smoke's stream down: without this, the first REAL frame
    // would CONTINUE the smoke's stream (P-frame) — but PROTOCOL §9.5
    // requires the first packet to be a keyframe, and the client
    // extracts SPS/PPS from the first keyframe. After this flush the
    // next input starts a fresh stream (IDR + fresh in-band SPS/PPS).
    {
      t->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
      DWORD d3 = now_ms() + 500;
      int idle = 0;  // consecutive 100 ms windows with no new event
      while (now_ms() < d3 && idle < 3) {
        IMFSample* o = nullptr;
        HRESULT hr = pull_one_output(t, gen, sink, now_ms() + 100, &o);
        if (hr == S_OK) {
          o->Release();
          idle = 0;
        } else {
          ++idle;
        }
      }
    }
  }

  // Success: adopt the MFT (members were untouched until now, so the
  // smoke used only local refs — the event generator was armed on
  // `sink`, which becomes ours).
  mft_ = t;
  gen_ = gen;
  mft_attrs_ = attrs;
  event_sink_ = sink;
  out_type_ = out;
  in_type_ = in;
  d3d_mgr_ = mgr;
  d3d11_ = dev;
  streaming_ = true;
  return true;
}

void MfH264Encoder::pumpUntil(DWORD deadline_ms) {
  if (!event_sink_ || !gen_ || !mft_ || dead_) return;
  while (now_ms() < deadline_ms && !dead_) {
    DWORD wr = WaitForSingleObject(event_sink_->have_event(), 25);
    if (wr != WAIT_OBJECT_0) return;
    IMFAsyncResult* ar = event_sink_->take_pending();
    if (!ar) continue;
    dispatchEvent(ar);
  }
}

void MfH264Encoder::dispatchEvent(IMFAsyncResult* ar) {
  if (!gen_ || !mft_) {
    ar->Release();
    return;
  }
  IMFMediaEvent* ev = nullptr;
  HRESULT he = gen_->EndGetEvent(ar, &ev);
  ar->Release();
  if (FAILED(he) || !ev) return;
  DWORD met = 0;
  ev->GetType(&met);
  if (met == static_cast<DWORD>(METransformNeedInput)) {
    if (!input_queue_.empty()) {
      QueuedFrame q = input_queue_.front();
      input_queue_.erase(input_queue_.begin());
      HRESULT hp = mft_->ProcessInput(0, q.sample, 0);
      if (SUCCEEDED(hp)) {
        input_seqs_.push_back(q.seq);
        q.sample->Release();
      } else {
        // Stale NeedInput (queue actually full — measured with QSV):
        // re-queue at the front; the next event decides.
        input_queue_.insert(input_queue_.begin(), q);
      }
    }
  } else if (met == static_cast<DWORD>(METransformHaveOutput)) {
    IMFSample* oSample = nullptr;
    MFCreateSample(&oSample);
    IMFMediaBuffer* oBuf = nullptr;
    MFCreateMemoryBuffer(8 * 1024 * 1024, &oBuf);
    oSample->AddBuffer(oBuf);
    oBuf->Release();
    MFT_OUTPUT_DATA_BUFFER odb;
    memset(&odb, 0, sizeof(odb));
    odb.dwStreamID = 0;
    odb.pSample = oSample;
    DWORD ost = 0;
    HRESULT ho = mft_->ProcessOutput(0, 1, &odb, &ost);
    if (SUCCEEDED(ho) && odb.pSample) {
      IMFSample* rs = odb.pSample;
      if (rs != oSample) oSample->Release();
      deliverOutput(rs);
      rs->Release();
    } else {
      // Measured: S_OK + null sample == NO_SAMPLE (the MFT is not
      // ready); wait for the NEXT event. Ticking or re-calling
      // ProcessOutput here corrupts the MFT state (probe phase).
      oSample->Release();
    }
  } else if (met == static_cast<DWORD>(METransformDrainComplete)) {
    flush_pending_ = false;
  } else if (met == static_cast<DWORD>(MEError)) {
    HRESULT st = S_OK;
    ev->GetStatus(&st);
    char buf[64];
    snprintf(buf, sizeof(buf), "mft_error_%08X", static_cast<unsigned>(st));
    sink_.onEncoderError(window_id_, buf);
    dead_ = true;
  }
  ev->Release();
  ResetEvent(event_sink_->have_event());
  if (!dead_) {
    if (FAILED(gen_->BeginGetEvent(event_sink_, nullptr))) {
      dead_ = true;  // the MFT event stream is gone
    }
  }
}

void MfH264Encoder::deliverOutput(IMFSample* sample) {
  std::vector<uint8_t> payload;
  if (!sample_bytes(sample, &payload) || payload.empty()) return;
  bool keyframe = false;
  UINT32 cp = 0;
  if (SUCCEEDED(sample->GetUINT32(MFSampleExtension_CleanPoint, &cp))) {
    keyframe = (cp != 0);
  }
  if (!keyframe) {
    keyframe = has_idr_nal(payload.data(), payload.size());
  }
  EncodedFrame ef;
  ef.window_id = window_id_;
  ef.frame_seq = out_frame_seq_++;
  ef.keyframe = keyframe;
  ef.payload = std::move(payload);
  if (!input_seqs_.empty()) {
    ef.source_seq = input_seqs_.front();
    input_seqs_.erase(input_seqs_.begin());
  }
  sink_.onEncodedFrame(ef);
}

bool MfH264Encoder::encodeFrame(const CapturedFrame& frame) {
  if (!started_ || dead_ || !mft_) return false;
  if (frame.format != core::CaptureFrameFormat::Bgra8 || frame.width == 0 ||
      frame.height == 0 ||
      frame.pixels.size() <
          static_cast<size_t>(frame.width) * frame.height * 4) {
    return false;
  }
  if (flush_pending_) {
    pumpUntil(now_ms() + 2000);  // wait for the pending flush to finish
    if (flush_pending_ || dead_ || !mft_) return false;
  }

  // BGRA -> NV12 (measured seam format: tight B8G8R8A8).
  std::vector<uint8_t> nv12 = bgra_to_nv12(frame);
  IMFSample* sample = nullptr;
  if (FAILED(make_nv12_sample(frame.width, frame.height, fps_, frame.seq,
                              nv12, &sample))) {
    return false;
  }

  HRESULT hp = mft_->ProcessInput(0, sample, 0);
  if (SUCCEEDED(hp)) {
    input_seqs_.push_back(frame.seq);
    sample->Release();
  } else if (hp == kMfNotAccepting) {
    // Queue full: hold the frame (measured: QSV queue depth 4 with
    // stale NeedInput events). Bounded queue; a persistently full
    // queue is backpressure (the pipeline's FpsGate prevents this
    // in steady state).
    if (input_queue_.size() >= 8) {
      sample->Release();
      return false;
    }
    input_queue_.push_back({sample, frame.seq});
  } else {
    sample->Release();
    sink_.onEncoderError(window_id_, "encode_failed");
    return false;
  }

  // Drain any output that is ready (bounded, short).
  pumpUntil(now_ms() + 10);
  return started_ && !dead_ && mft_ != nullptr;
}

void MfH264Encoder::requestKeyframe() {
  if (!started_ || dead_ || !mft_) return;
  // Best-effort codec-API property (codecapi.h): honored by MFTs that
  // implement the Codec API; harmless otherwise.
  if (mft_attrs_) {
    mft_attrs_->SetUINT32(CODECAPI_AVEncVideoForceKeyFrame, 1);
  }
  // MFT_MESSAGE_COMMAND_FLUSH (verified mftransform.h SDK 26100, value
  // 0): discards encoder state; the NEXT encoded frame starts a new
  // stream -> IDR with fresh in-band SPS/PPS. The client's VideoPipeline
  // resets its decoder before decoding a keyframe after a gap and
  // accepts in-band SPS/PPS in mid-stream access units.
  //
  // If data is in flight, wait for METransformDrainComplete (bounded).
  // If nothing is in flight the flush is a no-op that may NOT signal
  // DrainComplete — give it a short bounded pump to consume any events
  // and then clear the flag unconditionally (a stuck flush_pending_
  // would wedge the backend).
  const bool has_pending = !(input_seqs_.empty() && input_queue_.empty());
  mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
  if (has_pending) {
    flush_pending_ = true;
    pumpUntil(now_ms() + 2000);
  } else {
    pumpUntil(now_ms() + 300);
    flush_pending_ = false;
  }
}

std::vector<MfEncoderCandidate> MfH264Encoder::surveyCandidates() {
  std::vector<MfEncoderCandidate> out;
  MFT_REGISTER_TYPE_INFO filter;
  memset(&filter, 0, sizeof(filter));
  filter.guidMajorType = MFMediaType_Video;
  filter.guidSubtype = MFVideoFormat_H264;
  IMFActivate** arr = nullptr;
  UINT32 count = 0;
  HRESULT he = MFTEnum2(MFT_CATEGORY_VIDEO_ENCODER,
                        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_ASYNCMFT |
                            MFT_ENUM_FLAG_HARDWARE,
                        nullptr, &filter, nullptr, &arr, &count);
  if (FAILED(he) || !arr) return out;
  for (UINT32 i = 0; i < count; i++) {
    MfEncoderCandidate c;
    LPWSTR nm = nullptr;
    UINT32 nlen = 0;
    if (SUCCEEDED(arr[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute,
                                             &nm, &nlen)) &&
        nm) {
      c.friendly_name.assign(nlen, '\0');
      for (UINT32 k = 0; k < nlen; k++)
        c.friendly_name[k] = static_cast<char>(nm[k]);
      CoTaskMemFree(nm);
    }
    IMFTransform* t = nullptr;
    HRESULT ha = arr[i]->ActivateObject(IID_PPV_ARGS(&t));
    if (SUCCEEDED(ha) && t) {
      c.activatable = true;
      t->Release();
    } else {
      char buf[32];
      snprintf(buf, sizeof(buf), "activate=%08X",
               static_cast<unsigned>(ha));
      c.detail = buf;
    }
    arr[i]->Release();
    out.push_back(std::move(c));
  }
  CoTaskMemFree(arr);
  return out;
}

}  // namespace remotewindows::win32
