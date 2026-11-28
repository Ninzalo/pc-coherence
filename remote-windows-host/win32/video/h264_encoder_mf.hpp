#ifndef RWH_WIN32_VIDEO_H264_ENCODER_MF_HPP
#define RWH_WIN32_VIDEO_H264_ENCODER_MF_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/capture_encoder.hpp"
#include "core/capture_source.hpp"

// Win32-only header. The include order below is MEASURED (SDK
// 10.0.26100, MSVC 14.51, c++20) and probe-verified in the RW-H-012
// session; do not reorder it: the MF/COM includes stay at global
// scope (never inside a namespace), and the IMF* forward decls stay
// before those includes. Either reordering breaks a later system
// header during the first real translation unit that uses it.
struct IMFTransform;
struct IMFMediaEventGenerator;
struct IMFAttributes;
struct IMFDXGIDeviceManager;
struct ID3D11Device;
struct ID3D12Device;
struct IMFMediaType;
struct IMFActivate;
struct IMFSample;
struct IMFAsyncResult;
struct IMFMediaEvent;

#include <windows.h>  // HANDLE/CRITICAL_SECTION/DWORD/HRESULT
#include <mfapi.h>    // IMFAsyncCallback (SDK 26100 vtable) + MF base

// RW-H-012 (H-MEDIA): in-box Media Foundation H.264 encoder backend for the
// portable core::IVideoEncoder seam (core/capture_encoder.hpp), Win32-only.
//
// MEASURED against SDK 10.0.26100 on the dev PC (RTX 5080 + Intel iGPU,
// Win11 26H2; session-context dependence recorded in docs/STATUS.md):
//
// * The in-box H.264 encoder MFTs are ASYNC: they require
//   MF_TRANSFORM_ASYNC_UNLOCK on their attributes plus the
//   IMFMediaEventGenerator event model (BeginGetEvent / METransform*).
//   Direct sync ProcessOutput polling is not a supported path.
// * Candidate selection is BY BEHAVIOR, not by registry order: start()
//   probes each candidate on a FRESH, disposable MFT (activation,
//   negotiation, an 8-frame throwaway smoke encode — the measured
//   healthy burst, see tryCandidate) and selects the first one that
//   actually PRODUCES output in this process/session. The throwaway
//   MFT is released before adoption; the production MFT is a FRESH
//   instance so the client's first frame is its first input (no
//   retained/dropped smoke frames). Candidates that negotiate but
//   produce no output (e.g. Intel QSV in a headless session-0 context
//   on the dev PC) are rejected at start, so a failed start is honest
//   (the pipeline reports "start_failed") instead of a silent
//   zero-byte stream.
// * The measured output layout (Microsoft AVC DX12 Encoder, 8-frame
//   burst — the one reliably healthy mode on the dev PC): Annex-B
//   (4-byte 00 00 00 01 start codes), one access unit per output
//   sample, SPS(7)+PPS(8)+IDR(5) in-band in the first access unit,
//   1:1 input->output frames, MFSampleExtension_CleanPoint=1 on the IDR
//   sample and 0 otherwise. This matches the client decode contract
//   (remote-windows-client/.../Video/VideoPipeline.swift:10-21,131-166):
//   Annex-B wire payload, SPS/PPS in-band in the first keyframe,
//   3- or 4-byte start codes both accepted, client-side Annex-B -> AVCC.
// * MEASURED ENVELOPE (dev PC, 2026-11-28, Microsoft AVC DX12): the
//   ONLY reliably healthy mode is an 8-frame burst -> 8 outputs ->
//   END_OF_STREAM -> release (probe7e, clean exit 0). Real-time
//   sustained interleaved encoding is NOT available on this PC: pulling
//   output #1 early, a mid-stream COMMAND_FLUSH, a mid-lifetime
//   EOS+SOS restart, or sustained input interleaved with pulling past
//   ~24 outputs all wedge the encoder (silent, no MEError) or hard-kill
//   the host process (exit 3, no WER event). The app layer (H-FOUND)
//   must own frame pacing/backpressure and stream re-establish (EOS ->
//   fresh MFT -> IDR) when the envelope is exceeded.
// * On-demand keyframe (requestKeyframe) uses MFT_MESSAGE_COMMAND_FLUSH
//   (verified mftransform.h, SDK 26100): after a mid-stream flush the MFT
//   restarts the stream, so the next encoded frame is an IDR with fresh
//   in-band SPS/PPS. CODECAPI_AVEncVideoForceKeyFrame (codecapi.h) is
//   additionally set best-effort on the MFT attributes.
//
// THREADING (RW-H-013 lesson: no detached threads): the backend owns NO
// thread. All MFT calls run on the calling thread (the per-session
// capture worker). The only foreign entry point is the MFT's async
// callback (EventSink::Invoke, on the MFT's internal thread), which only
// touches a critical-section-guarded slot (same pattern as the probe
// phase). Lifetime: the MFT holds a COM ref on the callback while it
// exists; stop() performs EOF + bounded drain and then releases the MFT,
// after which no callback can fire; the callback self-deletes at
// refcount 0.

namespace remotewindows::win32 {

// Async-callback sink (SDK 26100 IMFAsyncCallback: GetParameters +
// Invoke, mfobjects.h). The ONLY entry point that runs on the MFT's
// internal thread; it touches nothing but a critical-section-guarded
// slot. Self-deleting COM object: the MFT holds a ref while it exists;
// stop() releases the MFT before the last ref is dropped, so no
// Invoke can outlive the owning backend.
class EventSink final : public IMFAsyncCallback {
 public:
  EventSink();
  ~EventSink();

  EventSink(const EventSink&) = delete;
  EventSink& operator=(const EventSink&) = delete;

  // IUnknown
  HRESULT QueryInterface(REFIID riid, void** pp) override;
  ULONG AddRef() override;
  ULONG Release() override;

  // SDK 26100 vtable
  HRESULT GetParameters(DWORD* flags, DWORD* queue) override;
  HRESULT Invoke(IMFAsyncResult* ar) override;

  // Pump-thread API (called on the owning/calling thread only).
  HANDLE have_event() const;  // auto-reset event, signaled when pending
  IMFAsyncResult* take_pending();  // pops one, or nullptr; caller owns ref

 private:
  volatile LONG ref_ = 1;
  HANDLE have_ev_ = nullptr;
  CRITICAL_SECTION cs_;
  std::vector<IMFAsyncResult*> pending_;
};

// One candidate observed by surveyCandidates() / start().
struct MfEncoderCandidate {
  std::string friendly_name;  // MFT_FRIENDLY_NAME_Attribute
  bool activatable = false;   // ActivateObject succeeded in this session
  bool produced_output = false;  // smoke encode yielded >=1 output sample
  std::string detail;  // short token: "" | "activate=<hex>" |
                       // "negotiate_failed=<hex>" | "no_output_in_smoke"
};

// The COM set that setupMft() configures for one MFT candidate and
// releaseSetup() tears down. An added ref on each object; all-null
// when setupMft fails (so releaseSetup is a no-op on failure). The
// throwaway candidate (tryCandidate) and the adopted production MFT
// (startInner) both go through this.
struct MftSetup {
  struct IMFTransform* t = nullptr;
  struct IMFMediaEventGenerator* gen = nullptr;
  struct IMFAttributes* attrs = nullptr;
  struct IMFDXGIDeviceManager* mgr = nullptr;
  struct ID3D12Device* dev12 = nullptr;  // "Microsoft AVC" (DX12) only
  struct IMFMediaType* out = nullptr;
  struct IMFMediaType* in = nullptr;
  EventSink* sink = nullptr;  // ref 1 (ours) + 1 (MFT's)
  bool started = false;  // START_OF_STREAM sent (drives the EOS teardown)
};

// Media Foundation H.264 encoder implementing core::IVideoEncoder.
// One instance per capture session (FR-04). Created via
// MfH264EncoderFactory (core::IVideoEncoderFactory).
class MfH264Encoder final : public core::IVideoEncoder {
 public:
  // `sink` must outlive the encoder (seam contract).
  MfH264Encoder(std::string window_id, core::IEncodedFrameConsumer& sink);
  ~MfH264Encoder() override;  // calls stop() if still running

  MfH264Encoder(const MfH264Encoder&) = delete;
  MfH264Encoder& operator=(const MfH264Encoder&) = delete;

  bool start(uint32_t width, uint32_t height, uint32_t fps_hint) override;
  bool encodeFrame(const core::CapturedFrame& frame) override;
  void requestKeyframe() override;
  void stop() override;  // idempotent; safe after a failed start

  // Caller-driven event pump: the ONLY way the MFT's outputs reach the
  // sink. The backend owns NO thread (RW-H-013 lesson) — the session
  // worker thread calls this to dispatch MFT events (outputs, flush
  // DrainComplete, MEError). `timeout_ms` is a RELATIVE bound: it pumps
  // for at most that long. encodeFrame/requestKeyframe/stop pump
  // internally; the test and the app-layer capture worker pump during
  // their waits. Safe to call any time (no-op when not started/dead).
  void pumpUntil(uint32_t timeout_ms);

  // Enumerate the in-box H.264 encoder MFTs and report what this
  // process/session can actually use (activation only — no encode; safe
  // in any session context). Used by tests and diagnostics.
  static std::vector<MfEncoderCandidate> surveyCandidates();

  // Friendly name of the MFT selected by start() ("" if none — either
  // not started or start failed). Measured, not assumed.
  const std::string& selectedEncoder() const { return selected_; }

  // True after an MFT-level MEError (the instance is unusable; encodeFrame
  // returns false until stop()).
  bool dead() const { return dead_; }

 private:
  // Full setup of one candidate MFT: activate + async unlock + (D3D12
  // for "Microsoft AVC") + output/input negotiation + BeginGetEvent +
  // START_OF_STREAM. On success the COM set is handed to `*s` (an added
  // ref on each object; releaseSetup is the only teardown). On failure
  // `*s` stays all-null and `*detail` carries a short token.
  // Caller-driven: runs on the calling thread.
  bool setupMft(IMFActivate* act, const std::string& name, uint32_t w,
                uint32_t h, uint32_t fps, MftSetup* s, std::string* detail);
  // Tear down a setupMft() set. Post-START_OF_STREAM it sends
  // END_OF_STREAM + one bounded drain pull BEFORE releasing (releasing
  // a started MFT without EOS can crash the host process — measured
  // 0xC0000409 on the restart path, 0xC0000005 on the NVIDIA path).
  // Safe on an all-null set (a failed setupMft).
  void releaseSetup(MftSetup* s);

  // Throwaway candidate verification: 8-frame burst into a FRESH,
  // disposable MFT, verify the first AU is the stream-start IDR +
  // in-band SPS/PPS, then END_OF_STREAM + release. The candidate MFT is
  // never adopted (startInner adopts a separate fresh instance) — no
  // retained/dropped smoke frames. See the probe7e measurement in the
  // .cpp for the full measured envelope.
  bool tryCandidate(IMFActivate* act, const std::string& name,
                    uint32_t w, uint32_t h, uint32_t fps,
                    std::string* detail);
  void dispatchEvent(IMFAsyncResult* ar);
  // Contain a C++ exception thrown by the MFT across the COM boundary
  // (MSVC does not stop the unwind at COM calls; measured 2026-11-28
  // the stalled Microsoft AVC DX12 MFT took the host process down via
  // std::terminate, exit 3). Marks the instance dead; the process
  // must never die because of an MFT.
  void markMftAbort();
  // Body of start(); wrapped so a MFT exception cannot escape.
  bool startInner(uint32_t width, uint32_t height, uint32_t fps_hint);
  void deliverOutput(IMFSample* sample);
  void releaseOwned();  // release all COM objects owned by this instance
  static std::vector<uint8_t> bgraToNv12(const core::CapturedFrame& f);

  std::string window_id_;
  core::IEncodedFrameConsumer& sink_;
  std::string selected_;

  // MFT plumbing (owned by this object; released by releaseOwned()).
  IMFTransform* mft_ = nullptr;
  IMFMediaEventGenerator* gen_ = nullptr;  // QI'd from mft_
  IMFAttributes* mft_attrs_ = nullptr;
  IMFDXGIDeviceManager* d3d_mgr_ = nullptr;
  ID3D12Device* d3d12_ = nullptr;  // "Microsoft AVC" (DX12) candidate only
  IMFMediaType* out_type_ = nullptr;
  IMFMediaType* in_type_ = nullptr;
  EventSink* event_sink_ = nullptr;  // ref 1 (ours) + 1 (MFT's)
  // CALLER-DRIVEN (RW-H-013 lesson + 2026-11-28 incident fix): the
  // backend owns NO thread. All MFT calls run on the calling thread
  // (the per-session capture worker); delivery does not depend on a
  // pump thread, so the test/app must pump (pumpUntil) during its
  // waits. input_seqs_ / flush_pending_ are touched only from that
  // thread; the only foreign entry point is the MFT's async callback
  // (EventSink::Invoke, on an MF worker thread), which only touches a
  // critical-section-guarded slot.
  CRITICAL_SECTION q_cs_;
  bool q_cs_inited_ = false;
  std::vector<uint64_t> input_seqs_;  // submitted-input seqs awaiting output
  uint32_t out_frame_seq_ = 0;
  uint32_t width_ = 0, height_ = 0, fps_ = 0;
  std::atomic<bool> flush_pending_{false};  // cleared on DrainComplete
  // No output delivered since the last stream start (START_OF_STREAM
  // or completed flush): the next encoded frame is going to be an IDR
  // with fresh in-band SPS/PPS anyway, so requestKeyframe() can skip
  // the MFT flush (measured 2026-11-28: an empty COMMAND_FLUSH right
  // after START_OF_STREAM leaves the Microsoft AVC DX12 encoder
  // producing no output at all).
  bool stream_fresh_ = false;
  bool streaming_ = false;  // START_OF_STREAM sent, EOF not yet
  bool started_ = false;
  std::atomic<bool> dead_{false};
  bool com_inited_here_ = false;
  uint32_t com_thread_id_ = 0;  // GetCurrentThreadId() (DWORD, 32-bit)
  // now_ms() of the last delivered output; 0 = no output yet in the
  // current stream. Feeds the wedge detector in stop() (measured
  // 2026-11-28: the in-box Microsoft AVC DX12 MFT hard-stalls
  // mid-stream on the dev PC).
  DWORD last_output_ms_ = 0;
  // Set by stop() when it leaked the owned COM set because the MFT
  // was wedged (releasing a wedged MFT terminates the process from
  // an MFT worker thread — measured 2026-11-28, exit 3, no WER
  // event; the leak is the only safe option and is bounded to one
  // set per wedged session).
  bool wedged_leaked_ = false;
};

// core::IVideoEncoderFactory over MfH264Encoder (app-layer wiring, H-FOUND,
// will use this in the registry/capture integration task).
class MfH264EncoderFactory final : public core::IVideoEncoderFactory {
 public:
  std::unique_ptr<core::IVideoEncoder> create(
      const std::string& window_id,
      core::IEncodedFrameConsumer& sink) override {
    return std::make_unique<MfH264Encoder>(window_id, sink);
  }
};

}  // namespace remotewindows::win32

#endif  // RWH_WIN32_VIDEO_H264_ENCODER_MF_HPP
