#ifndef RWH_WIN32_VIDEO_H264_ENCODER_MF_HPP
#define RWH_WIN32_VIDEO_H264_ENCODER_MF_HPP

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
//   probes each candidate (activation, negotiation, bounded 2-frame smoke
//   encode) and selects the first one that actually PRODUCES output in
//   this process/session. Candidates that negotiate but produce no output
//   (e.g. Intel QSV in a headless session-0 context on the dev PC) are
//   rejected at start, so a failed start is honest (the pipeline reports
//   "start_failed") instead of a silent zero-byte stream.
// * The measured output layout (NVIDIA H.264 Encoder MFT, 30-frame run):
//   Annex-B (4-byte 00 00 00 01 start codes), one access unit per output
//   sample, SPS(7)+PPS(8)+IDR(5) in-band in the first access unit,
//   1:1 input->output frames, MFSampleExtension_CleanPoint=1 on the IDR
//   sample and 0 otherwise. This matches the client decode contract
//   (remote-windows-client/.../Video/VideoPipeline.swift:10-21,159-166):
//   Annex-B wire payload, SPS/PPS in-band in the first keyframe,
//   3- or 4-byte start codes both accepted, client-side Annex-B -> AVCC.
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
  struct QueuedFrame {
    struct IMFSample* sample;
    uint64_t seq;
  };

  bool tryCandidate(IMFActivate* act, const std::string& name,
                    uint32_t w, uint32_t h, uint32_t fps,
                    std::string* detail);
  void pumpUntil(DWORD deadline_ms);  // dispatch queued MFT events
  void dispatchEvent(IMFAsyncResult* ar);
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
  ID3D11Device* d3d11_ = nullptr;
  IMFMediaType* out_type_ = nullptr;
  IMFMediaType* in_type_ = nullptr;
  EventSink* event_sink_ = nullptr;  // ref 1 (ours) + 1 (MFT's)
  std::vector<QueuedFrame> input_queue_;  // accepted-not-yet-submitted
  std::vector<uint64_t> input_seqs_;  // submitted-input seqs awaiting output
  uint32_t out_frame_seq_ = 0;
  uint32_t width_ = 0, height_ = 0, fps_ = 0;
  bool flush_pending_ = false;
  bool streaming_ = false;  // START_OF_STREAM sent, EOF not yet
  bool started_ = false;
  bool dead_ = false;
  bool com_inited_here_ = false;
  uint32_t com_thread_id_ = 0;  // GetCurrentThreadId() (DWORD, 32-bit)
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
