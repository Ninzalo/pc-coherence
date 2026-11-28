// RW-H-012 (H-MEDIA): in-box Media Foundation H.264 encoder backend.
//
// Design notes (measured against SDK 10.0.26100 on the dev PC - see the
// header and docs/STATUS.md):
//  * Candidate selection BY BEHAVIOR: start() probes each in-box H.264
//    encoder MFT (activate -> unlock -> negotiate -> bounded 2-frame
//    smoke encode) and selects the first that actually PRODUCES output
//    in this process/session. Measured on the dev PC 2026-11-28
//    (RTX 5080, Win11 26H2, interactive session): the "Microsoft AVC
//    DX12 Encoder" produces valid annex-B (SEI+SPS+PPS+IDR in the
//    first access unit) from system-memory NV12 input WITH a D3D12
//    device manager set (D3D12CreateDevice works; D3D11 device
//    creation is dead - E_INVALIDARG on every adapter incl. WARP,
//    which also takes the NVIDIA MFT out: it rejects CPU input with
//    MF_E_UNSUPPORTED_D3D_TYPE). Intel QSV: START_OF_STREAM fails or
//    dead NO_SAMPLE state; the sync MSH264 shim: 0 bytes across 9
//    probe variants. A failed start is therefore honest (the pipeline
//    reports "start_failed") instead of a silent zero-byte stream.
//  * Candidate fail discipline: any failure AFTER START_OF_STREAM
//    sends MFT_MESSAGE_NOTIFY_END_OF_STREAM + bounded drain before
//    the MFT is released. Measured: releasing a started NVIDIA MFT
//    without that teardown kills the process (0xC0000409, driver
//    fault); with it the same sequence survives (probe exit 0).
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
#include <d3d12.h>
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
#pragma comment(lib, "d3d12.lib")
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
    if (wr != WAIT_OBJECT_0) continue;  // silence: wait until deadline
    IMFAsyncResult* ar = sink->take_pending();
    if (!ar) continue;
    IMFMediaEvent* ev = nullptr;
    HRESULT he = gen->EndGetEvent(ar, &ev);
    ar->Release();
    // Every consumed event must be replaced with a fresh BeginGetEvent
    // BEFORE this function returns, or the MFT's next event has no
    // registered callback. Measured 2026-11-28 (Microsoft AVC DX12,
    // dev PC): the old code re-armed only at the loop tail, so the
    // S_OK early-return below left the subscription dead after the
    // smoke's first sample — no events were ever delivered again, the
    // MFT looked "stalled", and releasing it in that state killed the
    // host process (exit 3). Re-arm here, right after EndGetEvent.
    if (SUCCEEDED(he) && FAILED(gen->BeginGetEvent(sink, nullptr))) {
      if (ev) ev->Release();
      return E_FAIL;  // the event stream is gone
    }
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
        return S_OK;  // subscription re-armed above
      }
      oSample->Release();  // S_OK + NO_SAMPLE: keep waiting
    } else if (met == static_cast<DWORD>(MEError)) {
      ev->Release();
      return (status == S_OK) ? E_FAIL : status;
    }
    // METransformNeedInput etc.: nothing to feed from here.
    ev->Release();
  }
  return kNoSample;  // bounded timeout, no sample
}

// Bounded teardown of a stream that has already been STARTED:
// send END_OF_STREAM, drain briefly, and stop. Measured on the dev
// PC (NVIDIA H.264 MFT, interactive session, 2026-11-28): releasing
// a started MFT WITHOUT this teardown kills the host process (
// 0xC0000409 STATUS_STACK_BUFFER_OVERRUN, a driver fault in the
// NVIDIA MFT destructor); with END_OF_STREAM first the same
// sequence survives cleanly (probe: endofstream S_OK, drain 0
// bytes, release, process exit 0).
void tear_down_started_stream(IMFTransform* t,
                              IMFMediaEventGenerator* gen,
                              EventSink* sink) {
  if (!t) return;
  (void)t->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
  DWORD d = now_ms() + 300;
  int idle = 0;  // consecutive 100 ms windows with no new event
  while (now_ms() < d && idle < 2) {
    IMFSample* o = nullptr;
    HRESULT hr = pull_one_output(t, gen, sink, now_ms() + 100, &o);
    if (hr == S_OK) {
      if (o) o->Release();
      idle = 0;
    } else {
      ++idle;
    }
  }
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
// QUEUE: MFASYNC_CALLBACK_QUEUE_MULTITHREADED (measured value 5,
// mfobjects.h:4060-4068) - the callback lands on an MF worker thread;
// no message pump needed. Measured: MFASYNC_CALLBACK_QUEUE_STANDARD
// (1) on a bare MTA thread (no pump) never delivered Invoke, so the
// smoke encode always timed out ("no_output_in_smoke") even with a
// working NVIDIA MFT - the queue had to be MULTITHREADED.
HRESULT EventSink::GetParameters(DWORD* flags, DWORD* queue) {
  if (flags) *flags = 0;
  if (queue) *queue = MFASYNC_CALLBACK_QUEUE_MULTITHREADED;
  return S_OK;
}

HRESULT EventSink::Invoke(IMFAsyncResult* ar) {
  // Runs on an MF worker thread, called from inside the MFT: a C++
  // exception escaping here terminates the whole process outside any
  // of our handlers. Contain it (measured 2026-11-28, exit 3).
  HRESULT hr = S_OK;
  try {
    EnterCriticalSection(&cs_);
    if (ar) {
      ar->AddRef();  // hand a ref to the pump thread
      pending_.push_back(ar);
    }
    LeaveCriticalSection(&cs_);
  } catch (...) {
    hr = E_FAIL;
  }
  if (SUCCEEDED(hr) && have_ev_) SetEvent(have_ev_);
  return hr;
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
    : window_id_(std::move(window_id)), sink_(sink) {
  InitializeCriticalSection(&q_cs_);
  q_cs_inited_ = true;
}

MfH264Encoder::~MfH264Encoder() {
  stop();
  if (q_cs_inited_) DeleteCriticalSection(&q_cs_);
}

void MfH264Encoder::releaseOwned() {
  // A wedged MFT can throw C++ across the COM boundary on release
  // (measured 2026-11-28: releasing a stalled Microsoft AVC DX12 MFT
  // killed the host process — exit 3, no WER record). Catch-all: drop
  // the handles and leak the objects (bounded: one set per encoder
  // session) instead of dying. If stop() already marked a wedged set
  // (wedged_leaked_) there is nothing to release — releasing it would
  // terminate the process.
  if (!wedged_leaked_) {
    try {
      // Consume any events still queued so the MFT holds no pending
      // callback operation before it is released.
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
        gen_ = nullptr;  // may be the MFT's last external ref; the MFT
                         // then releases the callback ref it holds
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
      if (d3d12_) {
        d3d12_->Release();
        d3d12_ = nullptr;
      }
    } catch (...) {
      // The MFT object (and possibly a few companions) leak; everything
      // else in this instance is either released above or raw state.
      mft_ = nullptr;
      gen_ = nullptr;
      event_sink_ = nullptr;
      mft_attrs_ = nullptr;
      out_type_ = nullptr;
      in_type_ = nullptr;
      d3d_mgr_ = nullptr;
      d3d12_ = nullptr;
    }
  }
  input_seqs_.clear();
  flush_pending_ = false;
  streaming_ = false;
}

void MfH264Encoder::stop() {
  if (!started_) return;
  // CALLER-DRIVEN (RW-H-013 lesson + 2026-11-28 incident fix): the
  // backend owns NO thread, so there is no pump thread to join. The
  // calling thread is the only MFT caller from here on. A wedged MFT
  // can hard-stall mid-stream and releasing it from an MFT worker
  // thread terminates the host process (exit 3, no WER event); the
  // pump-thread design's bounded join was a 4-minute hang on a wedged
  // MFT, so stop() does no join and relies on the wedge-leak below.
  if (!dead_ && mft_) {
    bool pending = false;
    EnterCriticalSection(&q_cs_);
    pending = !input_seqs_.empty();
    LeaveCriticalSection(&q_cs_);
    const bool wedged = pending && last_output_ms_ != 0 &&
                        now_ms() - last_output_ms_ > 10000;
    if (wedged) {
      // Measured 2026-11-28 (Microsoft AVC DX12, dev PC, 4/4 runs):
      // the MFT can HARD-STALL mid-stream (no MEError, no events,
      // input still pending) and releasing a wedged MFT terminates
      // the process. On a wedge the owned COM set is leaked (bounded:
      // one set per wedged session) instead of released; the session
      // is unusable either way, and the leak is the only teardown
      // that does not kill the host.
      wedged_leaked_ = true;
      sink_.onEncoderError(window_id_, "encoder_wedged_leaked");
    } else {
      // End of stream + bounded drain (probe7e clean release: all
      // outputs consumed, EOS, drain, release, process exit 0).
      try {
        mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        pumpUntil(2000);
      } catch (...) {
        markMftAbort();
      }
    }
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
  try {
    return startInner(width, height, fps_hint);
  } catch (...) {
    // MFT exception during candidate negotiation / adoption: contain
    // it, tear down whatever was adopted, report the failure.
    markMftAbort();
    releaseOwned();
    started_ = false;
    if (com_inited_here_ && GetCurrentThreadId() == com_thread_id_) {
      CoUninitialize();
      com_inited_here_ = false;
    }
    return false;
  }
}

bool MfH264Encoder::startInner(uint32_t width, uint32_t height,
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

  // Measured on the dev PC 2026-11-28 (SDK 26100, Win11 26H2, RTX 5080):
  // the NVIDIA MFT activates and negotiates, rejects system-memory NV12
  // input (MF_E_UNSUPPORTED_D3D_TYPE) after START_OF_STREAM — it wants
  // D3D11-texture input, and D3D11 device creation is dead on this PC
  // (E_INVALIDARG on every adapter incl. WARP). Worse, releasing that
  // MFT after the START_OF_STREAM + rejected-input sequence crashes the
  // host process (unpreventable from client code). Fatal AND
  // non-functional here: skipped by name — the behavior smoke cannot
  // reject it safely.
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
    if (name.find("NVIDIA H.264") != std::string::npos) continue;
    if (name.find("Quick Sync") == std::string::npos &&
        name.find("Microsoft AVC") == std::string::npos) {
      continue;
    }
    order.push_back({arr[i], std::move(name)});
  }
  // (Candidate order: enumeration order — the NVIDIA-first sort was
  // dropped with the NVIDIA skip; QSV before MS-AVC by preference.)

  // THROWAWAY CANDIDATE VERIFICATION: probe each candidate on a FRESH,
  // disposable MFT (8-frame burst -> 8 outputs, verify the first AU is
  // the guaranteed stream-start IDR + in-band SPS/PPS, then EOS +
  // release). The candidate that actually PRODUCES valid output in this
  // process/session wins. Its MFT is released before adoption — it never
  // becomes the production stream (no retained/dropped smoke frames).
  int chosen = -1;
  for (size_t i = 0; i < order.size(); i++) {
    std::string detail;
    if (tryCandidate(order[i].act, order[i].name, width, height, fps_,
                     &detail)) {
      selected_ = order[i].name;
      // One line per start(): which encoder won (structured, low-noise;
      // the [h264-test] survey lines show the full candidate list).
      std::fprintf(stderr, "[h264-enc] selected=%s\n", order[i].name.c_str());
      chosen = static_cast<int>(i);
      break;
    }
    std::fprintf(stderr,
                 "[h264-enc] candidate=%s result=failed detail=%s\n",
                 order[i].name.c_str(), detail.c_str());
  }

  bool ok = false;
  if (chosen >= 0) {
    // ADOPT A FRESH MFT for production: a brand-new instance whose FIRST
    // stream is the production stream. The client's first frame is the
    // first input, and its first output (IDR + in-band SPS/PPS) is the
    // client's first packet — the PROTOCOL §9.5 first-packet rule,
    // structurally satisfied, with no retained/dropped smoke frames.
    // ActivateObject is safe to call again on the same activate object
    // (each call returns a new MFT instance).
    MftSetup s = {};
    std::string detail;
    if (setupMft(order[chosen].act, order[chosen].name, width, height, fps_,
                 &s, &detail)) {
      mft_ = s.t;
      gen_ = s.gen;
      mft_attrs_ = s.attrs;
      d3d_mgr_ = s.mgr;
      d3d12_ = s.dev12;
      out_type_ = s.out;
      in_type_ = s.in;
      event_sink_ = s.sink;
      streaming_ = true;
      stream_fresh_ = true;  // first client frame -> first IDR
      ok = true;
    } else {
      std::fprintf(stderr, "[h264-enc] adopt failed detail=%s\n",
                   detail.c_str());
      releaseSetup(&s);
    }
  }

  // ActivateObject hands out new objects; the enumeration's activate
  // objects are ours to release in ALL cases (the selected MFT's
  // lifetime is independent of its activate object).
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

// Full setup of one candidate MFT: activate + async unlock + (D3D12 for
// "Microsoft AVC") + output/input negotiation + BeginGetEvent +
// START_OF_STREAM. On success the whole COM set is handed to `*s` (an
// added ref on each object; `releaseSetup` is the only teardown). On
// failure `*s` stays all-null and `*detail` carries a short token.
bool MfH264Encoder::setupMft(IMFActivate* act, const std::string& name,
                             uint32_t w, uint32_t h, uint32_t fps,
                             MftSetup* s, std::string* detail) {
  *s = MftSetup{};  // zeroed on every exit; filled only on success
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

  // D3D11 manager: NOT SET. Measured on the dev PC 2026-11-28:
  // D3D11CreateDevice fails E_INVALIDARG for the NVIDIA RTX 5080, the
  // Intel iGPU, Microsoft Basic Render, the default (nullptr) adapter
  // AND WARP — the D3D11 stack is dead on this machine, while
  // D3D12CreateDevice succeeds.
  //
  // "Microsoft AVC" (DX12) candidate: measured 2026-11-28 to REQUIRE a
  // D3D12 device manager before negotiation (probes 7d/7e: without one
  // SetOutputType fails E_FAIL; with one, plain system-memory NV12 input
  // works and produces valid annex-B — no D3D texture plumbing needed).
  IMFDXGIDeviceManager* mgr = nullptr;
  ID3D12Device* dev12 = nullptr;
  if (name.find("Microsoft AVC") != std::string::npos) {
    if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&dev12)))) {
      UINT token = 0;
      if (SUCCEEDED(MFCreateDXGIDeviceManager(&token, &mgr)) && mgr) {
        mgr->ResetDevice(dev12, token);
      }
    }
    if (!mgr) {
      if (dev12) dev12->Release();
      if (attrs) attrs->Release();
      t->Release();
      *detail = "d3d12_unavailable";
      return false;
    }
  }

  auto fail = [t, attrs, mgr, dev12](std::string* d, const char* msg,
                                     HRESULT hr) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s=%08X", msg, static_cast<unsigned>(hr));
    *d = buf;
    if (attrs) attrs->Release();
    if (mgr) mgr->Release();
    if (dev12) dev12->Release();
    t->Release();
  };

  if (mgr) {
    HRESULT hset = t->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER,
                                     (ULONG_PTR)mgr);
    if (FAILED(hset)) {
      fail(detail, "set_d3d_manager", hset);
      return false;
    }
  }

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
  if (FAILED(hos)) {
    // Measured on the dev PC 2026-11-28 (probe 7d): the Microsoft AVC
    // DX12 encoder accepts a template output type carrying EXACTLY
    // size+rate, and rejects it with extra attributes (interlace mode,
    // all-samples-independent, ...) or with size alone. Retry with a
    // minimal type before rejecting the candidate. (For the other
    // candidates the decorated type is what the probes measured
    // accepted, so this is a no-op there.)
    IMFMediaType* outm = nullptr;
    HRESULT hm = t->GetOutputAvailableType(0, 0, &outm);
    if (SUCCEEDED(hm) && outm) {
      outm->SetUINT64(MF_MT_FRAME_SIZE,
                      (static_cast<UINT64>(w) << 32) | h);
      outm->SetUINT64(MF_MT_FRAME_RATE, pack_rate(fps, 1));
      HRESULT hm2 = t->SetOutputType(0, outm, 0);
      if (SUCCEEDED(hm2)) {
        out->Release();
        out = outm;
        hos = S_OK;
      } else {
        outm->Release();
      }
    }
  }
  if (FAILED(hos)) {
    fail(detail, "negotiate_failed", hos);
    out->Release();
    return false;
  }

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
    // Measured on the dev PC 2026-11-28 (probes 7d/7e): the Microsoft
    // AVC DX12 encoder rejects the decorated NV12 input type (the
    // attribute set above) but accepts a minimal one carrying only the
    // frame size; retry minimally before rejecting the candidate.
    for (DWORD i = 0; i < 8 && (FAILED(his) || !in); i++) {
      IMFMediaType* cand = nullptr;
      HRESULT h = t->GetInputAvailableType(0, i, &cand);
      if (h == MF_E_NO_MORE_TYPES || FAILED(h) || !cand) break;
      GUID sub;
      cand->GetGUID(MF_MT_SUBTYPE, &sub);
      if (IsEqualGUID(sub, MFVideoFormat_NV12)) {
        cand->SetUINT64(MF_MT_FRAME_SIZE,
                        (static_cast<UINT64>(w) << 32) | h);
        HRESULT hm = t->SetInputType(0, cand, 0);
        if (SUCCEEDED(hm)) {
          in = cand;
          his = S_OK;
          break;
        }
      }
      cand->Release();
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

  // Success: hand the whole set to the caller (an added ref on each).
  s->t = t;
  s->gen = gen;
  s->attrs = attrs;
  s->mgr = mgr;
  s->dev12 = dev12;
  s->out = out;
  s->in = in;
  s->sink = sink;
  s->started = true;
  return true;
}

// Release everything setupMft handed out. Post-START_OF_STREAM:
// END_OF_STREAM + one bounded drain pull FIRST — releasing a started
// MFT without EOS can crash the host process (measured 2026-11-28:
// 0xC0000409 on the restart path, 0xC0000005 on the NVIDIA path).
void MfH264Encoder::releaseSetup(MftSetup* s) {
  if (s->t && s->started) {
    (void)s->t->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    IMFSample* o = nullptr;
    if (s->gen && s->sink) {
      (void)pull_one_output(s->t, s->gen, s->sink, now_ms() + 5000, &o);
    }
    if (o) o->Release();
  }
  if (s->sink) s->sink->Release();
  if (s->gen) s->gen->Release();
  if (s->out) s->out->Release();
  if (s->in) s->in->Release();
  if (s->attrs) s->attrs->Release();
  if (s->mgr) s->mgr->Release();
  if (s->dev12) s->dev12->Release();
  if (s->t) s->t->Release();
  s->t = nullptr;
  s->gen = nullptr;
  s->attrs = nullptr;
  s->mgr = nullptr;
  s->dev12 = nullptr;
  s->out = nullptr;
  s->in = nullptr;
  s->sink = nullptr;
  s->started = false;
}

// THROWAWAY CANDIDATE VERIFICATION (the probe7e cycle): an 8-frame burst
// into a FRESH, disposable MFT, pull its outputs, verify the first
// access unit is the guaranteed stream-start IDR + in-band SPS/PPS
// (probe7e: first AU = SEI+SPS+PPS+IDR), then END_OF_STREAM + release.
// probe7e (2026-11-28, dev PC, Microsoft AVC DX12) measured this exact
// sequence as the ONE reliably healthy state for this in-box encoder:
// 8 accepted -> 8 outputs, valid annex-B, clean release, process exit 0.
// Measured 2026-11-28, same PC, every deviation from that state wedges
// the encoder (silent, no MEError) or kills the host process (exit 3,
// no WER event): pulling the first output early while feeding more
// input, a mid-stream COMMAND_FLUSH (empty or with in-flight data), a
// mid-lifetime END_OF_STREAM + START_OF_STREAM restart, draining the
// smoke tail before the client stream, or sustained input interleaved
// with output pulling beyond ~24 outputs. The throwaway therefore keeps
// the production MFT's first stream INTACT: startInner adopts a FRESH
// MFT for the client, so the client's first frame is the first input
// and its first output (IDR + in-band SPS/PPS) is the client's first
// packet — the PROTOCOL §9.5 first-packet rule with no retained/dropped
// smoke frames. A candidate that negotiates but never produces output
// (QSV: START_OF_STREAM fails on the dev PC; the sync MSH264 shim: 0
// bytes) fails here and is rejected — start() stays honest about the
// environment instead of delivering a silent zero-byte stream.
bool MfH264Encoder::tryCandidate(IMFActivate* act, const std::string& name,
                                 uint32_t w, uint32_t h, uint32_t fps,
                                 std::string* detail) {
  MftSetup s = {};
  if (!setupMft(act, name, w, h, fps, &s, detail)) return false;
  auto* t = s.t;
  auto* gen = s.gen;
  auto* sink = s.sink;

  // The first output pulled anywhere (feed back-pressure or the pull
  // loop below) must be the stream-start AU: SPS + PPS + IDR.
  bool first_au_ok = false;
  bool saw_first = false;
  auto consume = [&](IMFSample* o) {
    if (!saw_first) {
      std::vector<uint8_t> bytes;
      if (sample_bytes(o, &bytes)) {
        first_au_ok =
            has_sps_pps_nals(bytes.data(), bytes.size()) &&
            has_idr_nal(bytes.data(), bytes.size());
      }
      saw_first = true;
    }
    o->Release();
  };

  // --- Feed 8 frames (the measured accept limit / healthy burst) -----
  const DWORD plane = w * h;
  const DWORD cb = plane + plane / 2;
  std::vector<uint8_t> nv12(cb);
  for (DWORD f = 0; f < 8; f++) {
    for (DWORD y = 0; y < h; y++) {
      for (DWORD x = 0; x < w; x++) {
        nv12[y * w + x] = static_cast<uint8_t>((x + y + f * 32) & 0xFF);
      }
    }
    for (DWORD i = 0; i < plane / 2; i++) {
      nv12[plane + i] = static_cast<uint8_t>(
          0x80 + static_cast<int>((i + f * 16) % 32) - 16);
    }
    IMFSample* sp = nullptr;
    if (FAILED(make_nv12_sample(w, h, fps, f, nv12, &sp))) {
      releaseSetup(&s);
      *detail = "smoke_alloc_failed";
      return false;
    }
    HRESULT hp = t->ProcessInput(0, sp, 0);
    if (hp == kMfNotAccepting) {
      // Queue full: pump until the MFT frees room (bounded — absorbs
      // the cold start), then retry.
      const DWORD d = now_ms() + 8000;
      bool fed = false;
      while (now_ms() < d && !fed) {
        IMFSample* o = nullptr;
        (void)pull_one_output(t, gen, sink, now_ms() + 50, &o);
        if (o) consume(o);
        if (SUCCEEDED(t->ProcessInput(0, sp, 0))) fed = true;
      }
      if (!fed) {
        sp->Release();
        releaseSetup(&s);
        *detail = "smoke_queue_full";
        return false;
      }
    } else if (FAILED(hp)) {
      // Input rejected with a hard error (measured on the dev PC: the
      // NVIDIA MFT answers MF_E_UNSUPPORTED_D3D_TYPE to system-memory
      // NV12). releaseSetup tears down BEFORE release — a started MFT
      // released without END_OF_STREAM can crash the process.
      sp->Release();
      char buf[48];
      snprintf(buf, sizeof(buf), "input_rejected=%08X",
               static_cast<unsigned>(hp));
      releaseSetup(&s);
      *detail = buf;
      return false;
    }
    sp->Release();
  }

  // --- Pull up to 8 outputs (the measured 1:1 pattern) ---------------
  int got = 0;
  const DWORD deadline = now_ms() + 20000;
  while (got < 8 && now_ms() < deadline) {
    IMFSample* o = nullptr;
    HRESULT hr = pull_one_output(t, gen, sink, now_ms() + 100, &o);
    if (hr == kNoSample) continue;  // no sample yet; bounded by deadline
    if (FAILED(hr)) {
      releaseSetup(&s);
      *detail = saw_first ? "smoke_pull_error" : "no_output_in_smoke";
      return false;
    }
    consume(o);
    ++got;
  }

  // EOS + bounded drain + release (safe teardown, probe7e clean exit).
  releaseSetup(&s);

  if (!saw_first) {
    // Negotiated and started but never produced output (measured on the
    // dev PC: the QSV MFT dead state). Honest rejection.
    *detail = "no_output_in_smoke";
    return false;
  }
  if (!first_au_ok) {
    *detail = "first_au_missing_sps_pps_idr";
    return false;
  }
  return true;
}

void MfH264Encoder::markMftAbort() {
  bool expected = false;
  if (!dead_.compare_exchange_strong(expected, true)) return;
  try {
    sink_.onEncoderError(window_id_, "mft_cxx_exception");
  } catch (...) {
    // Reporting is best-effort: dead_ is set; the instance is unusable
    // either way, and this may run inside a catch block where a second
    // throw would terminate the process.
  }
}

void MfH264Encoder::pumpUntil(uint32_t timeout_ms) {
  // Caller-driven event pump: the ONLY way the MFT's outputs reach the
  // sink. The backend owns NO thread — the caller drives this from the
  // session worker thread. Pumps (dispatches) events until now_ms()
  // reaches `now + timeout_ms`. Re-arming BeginGetEvent after every
  // EndGetEvent is the MFT contract (measured 2026-11-28: failing to
  // re-arm leaves the event stream dead).
  if (!event_sink_ || !gen_ || !mft_ || dead_) return;
  const DWORD deadline = now_ms() + timeout_ms;
  while (now_ms() < deadline && !dead_) {
    DWORD wr = WaitForSingleObject(event_sink_->have_event(), 25);
    if (wr != WAIT_OBJECT_0) continue;  // idle: keep pumping to deadline
    try {
      IMFAsyncResult* ar = event_sink_->take_pending();
      if (!ar) continue;
      dispatchEvent(ar);
    } catch (...) {
      // A MFT C++ exception unwinds across the COM boundary here
      // (measured 2026-11-28); uncaught it would std::terminate the
      // whole process. Mark dead and stop pumping.
      markMftAbort();
      return;
    }
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
  // Re-arm EARLY — right after EndGetEvent, BEFORE processing (the
  // working pull_one_output pattern, ~line 251). Measured 2026-11-28
  // (this PC, Microsoft AVC DX12): re-arming only at the loop tail and
  // ResetEvent-ing the have-event slot after processing let a concurrent
  // Invoke's SetEvent be lost (stall), and the pull-while-feeding
  // interleaving hard-faulted the MFT (SEH 0xc0000005 in the test body).
  // Re-arm here, before touching the event.
  if (SUCCEEDED(he)) {
    if (FAILED(gen_->BeginGetEvent(event_sink_, nullptr))) {
      dead_ = true;  // the MFT event stream is gone
      if (ev) ev->Release();
      return;
    }
  }
  if (FAILED(he) || !ev) return;
  DWORD met = 0;
  ev->GetType(&met);
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
    // Flush completed: the in-flight inputs were discarded and the next
    // output is a fresh-stream IDR.
    EnterCriticalSection(&q_cs_);
    const bool was_flush = flush_pending_;
    flush_pending_ = false;
    if (was_flush) input_seqs_.clear();  // discarded inputs
    LeaveCriticalSection(&q_cs_);
    if (was_flush) {
      stream_fresh_ = true;
      last_output_ms_ = now_ms();  // restart the wedge timer
    }
  } else if (met == static_cast<DWORD>(MEError)) {
    HRESULT st = S_OK;
    ev->GetStatus(&st);
    char buf[64];
    snprintf(buf, sizeof(buf), "mft_error_%08X", static_cast<unsigned>(st));
    sink_.onEncoderError(window_id_, buf);
    dead_ = true;
  }
  // (METransformNeedInput: ignored — inputs are submitted by
  // encodeFrame, not from the event handler.)
  ev->Release();
  // NO ResetEvent here: it races with Invoke's SetEvent and can lose a
  // concurrent event's notification (stall). The auto-reset event is
  // consumed by pumpUntil's WaitForSingleObject; the re-arm already
  // happened at the top (before processing).
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
  ef.keyframe = keyframe;
  ef.payload = std::move(payload);
  stream_fresh_ = false;  // the stream has output now; a mid-stream
                          // keyframe request needs a real flush
  last_output_ms_ = now_ms();
  // The MFT emits outputs in submission order (1:1, measured): the next
  // submitted input's seq is this output's source_seq.
  EnterCriticalSection(&q_cs_);
  if (!input_seqs_.empty()) {
    ef.source_seq = input_seqs_.front();
    input_seqs_.erase(input_seqs_.begin());
  }
  LeaveCriticalSection(&q_cs_);
  ef.frame_seq = out_frame_seq_++;
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
  try {
    // NO pre-pump here (measured 2026-11-28, this PC, Microsoft AVC
    // DX12): pulling an output while more input is still being fed
    // ("pull while feeding") is how this in-box MFT hard-faults (SEH
    // 0xc0000005 in the test body). Feed first; only pull when the MFT
    // says NOTACCEPTING (queue full, below) or when the caller pumps
    // (pumpUntil during its waits). This matches the throwaway smoke
    // (tryCandidate), the one measured-healthy control flow.
    // BGRA -> NV12 (measured seam format: tight B8G8R8A8).
    std::vector<uint8_t> nv12 = bgra_to_nv12(frame);
    IMFSample* sample = nullptr;
    if (FAILED(make_nv12_sample(frame.width, frame.height, fps_, frame.seq,
                                nv12, &sample))) {
      return false;
    }
    HRESULT hp = mft_->ProcessInput(0, sample, 0);
    if (hp == kMfNotAccepting) {
      // Queue full (MF_E_NOTACCEPTING): pump until the MFT frees room
      // (bounded 8 s — absorbs the cold start and one slow window),
      // then retry. A persistently full queue drops the frame (seam
      // contract: false = the pipeline counts the error and keeps the
      // session alive). Measured 2026-11-28 (Microsoft AVC DX12, dev
      // PC): the first frame of a fresh stream takes several seconds
      // (SPS/PPS + D3D12 setup) and the encoder sustains ~1-2 fps at
      // 320x200 with multi-second slow windows.
      const DWORD deadline = now_ms() + 8000;
      bool fed = false;
      while (!fed && now_ms() < deadline && !dead_) {
        pumpUntil(50);
        if (SUCCEEDED(mft_->ProcessInput(0, sample, 0))) fed = true;
      }
      if (!fed) {
        sample->Release();
        return false;
      }
    } else if (FAILED(hp)) {
      sample->Release();
      sink_.onEncoderError(window_id_, "encode_failed");
      return false;
    }
    // Accepted: record the seq for the next output's source_seq.
    EnterCriticalSection(&q_cs_);
    input_seqs_.push_back(frame.seq);
    LeaveCriticalSection(&q_cs_);
    sample->Release();
    return true;
  } catch (...) {
    // MFT exception across the COM boundary (or an allocation failure
    // in the conversion path): contain it — the seam reports the error
    // and the session stays alive.
    markMftAbort();
    return false;
  }
}

void MfH264Encoder::requestKeyframe() {
  if (!started_ || dead_ || !mft_) return;
  try {
    // Best-effort codec-API property (codecapi.h): honored by MFTs that
    // implement the Codec API; harmless otherwise.
    if (mft_attrs_) {
      mft_attrs_->SetUINT32(CODECAPI_AVEncVideoForceKeyFrame, 1);
    }
    // Fresh stream: the next output is already going to be an IDR with
    // fresh in-band SPS/PPS — skip the flush. Measured 2026-11-28 (dev
    // PC): an empty COMMAND_FLUSH right after START_OF_STREAM leaves
    // the Microsoft AVC DX12 encoder producing no output at all.
    if (stream_fresh_) return;
    // Data in flight: MFT_MESSAGE_COMMAND_FLUSH (verified mftransform.h
    // SDK 26100) discards encoder state; the NEXT encoded output starts
    // a new stream -> IDR with fresh in-band SPS/PPS. The client's
    // VideoPipeline resets its decoder before a keyframe after a gap
    // and accepts in-band SPS/PPS in mid-stream access units.
    //
    // Measured 2026-11-28 (dev PC, Microsoft AVC DX12): a mid-stream
    // COMMAND_FLUSH with in-flight data WEDGES the in-box encoder (no
    // DrainComplete, no output for 20 s). The wait below is bounded;
    // on timeout the stream continues (re-establish is the app layer's
    // job). A stuck flush must not wedge the backend.
    mft_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    EnterCriticalSection(&q_cs_);
    flush_pending_ = true;
    LeaveCriticalSection(&q_cs_);
    const DWORD deadline = now_ms() + 2000;
    while (now_ms() < deadline && !dead_) {
      bool fp = false;
      EnterCriticalSection(&q_cs_);
      fp = flush_pending_;
      LeaveCriticalSection(&q_cs_);
      if (!fp) break;
      pumpUntil(50);  // DrainComplete arrives via the event pump
    }
    if (flush_pending_) {
      // Stuck flush: clear the flag so the backend does not wedge.
      EnterCriticalSection(&q_cs_);
      flush_pending_ = false;
      LeaveCriticalSection(&q_cs_);
    }
  } catch (...) {
    markMftAbort();
  }
}

std::vector<MfEncoderCandidate> MfH264Encoder::surveyCandidates() {
  std::vector<MfEncoderCandidate> out;
  // ActivateObject is a COM call: the calling thread needs an
  // apartment. Measured (dev PC, interactive session, SDK 26100): on
  // a bare gtest main thread (no CoInitializeEx) every candidate
  // (including the working NVIDIA MFT) reported activate=0x800401F0
  // (CO_E_NOTINITIALIZED), masking real availability. Same pattern as
  // start(): init here only when this call did.
  bool com_here = false;
  if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
    com_here = true;
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
  if (FAILED(he) || !arr) {
    if (com_here) CoUninitialize();
    return out;
  }
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
  if (com_here) CoUninitialize();
  return out;
}


}  // namespace remotewindows::win32
