
#include "core/capture_session_manager.hpp"

#include <chrono>

namespace remotewindows::core {

namespace {

uint64_t monotonicUs() {
  using namespace std::chrono;
  return static_cast<uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch())
          .count());
}

}  // namespace

// --------------------------------------------------------------- internals

struct CaptureSessionManager::Session {
  std::string window_id;
  std::string hwnd;
  std::string app_session_id;
  uint32_t width_px = 0;
  uint32_t height_px = 0;
  uint32_t dpi = 100;
  bool minimized = false;
  bool auto_shared = false;
  bool surface_started = false;  // adapter surface is up
  CaptureSessionState state = CaptureSessionState::Inactive;

  FrameGate gate;  // non-movable; bound set via reset() at construction
  std::unique_ptr<IVideoEncoder> encoder;
  std::unique_ptr<IEncodedFrameConsumer> sink;  // per-session counting sink

  std::thread worker;
  std::mutex worker_mu;
  std::condition_variable worker_cv;
  std::atomic<bool> stop_worker{false};
  bool worker_joined = false;  // guarded by the manager mutex
  std::atomic<bool> fps_active{false};
  FpsGate fps_gate;  // guarded by worker_mu; the worker is its only user
  uint32_t recreate_attempts = 0;  // guarded by the manager mutex

  CaptureSessionMetrics metrics;
};

// Per-session IEncodedFrameConsumer: updates the session metrics, then
// forwards to the app-layer sink (the later PROTOCOL §9 channel).
struct CountingSink : public IEncodedFrameConsumer {
  CaptureSessionManager& mgr;
  std::string window_id;
  CountingSink(CaptureSessionManager& m, std::string id)
      : mgr(m), window_id(std::move(id)) {}
  void onEncodedFrame(const EncodedFrame& frame) override {
    mgr.noteEncoded(window_id, frame);
  }
  void onEncoderError(const std::string& window_id,
                      const std::string& reason) override {
    mgr.noteEncoderError(window_id, reason);
  }
};

struct CaptureSessionManager::Impl {
  ICaptureSurfaceSource& source;
  IVideoEncoderFactory& encoder_factory;
  IEncodedFrameConsumer& encoded_sink;
  CaptureManagerConfig config;
  mutable std::mutex mu;
  // Serializes teardown/recreate (worker join + encoder stop) so two
  // concurrent stops of the same session never double-join.
  std::mutex teardown_mu;
  std::map<std::string, std::unique_ptr<Session>> sessions;
};

// ------------------------------------------------------------- lifecycle

CaptureSessionManager::CaptureSessionManager(ICaptureSurfaceSource& source,
                                             IVideoEncoderFactory& encoder_factory,
                                             IEncodedFrameConsumer& encoded_sink,
                                             CaptureManagerConfig config)
    : impl_(std::unique_ptr<Impl>(new Impl{source, encoder_factory,
                                           encoded_sink, config})) {}

CaptureSessionManager::~CaptureSessionManager() {
  const std::vector<std::string> ids = sharedWindowIds();
  for (const std::string& id : ids) unshareWindow(id);
}

uint64_t CaptureSessionManager::nowUs() const { return monotonicUs(); }

bool CaptureSessionManager::geometryChanged(const Session& s,
                                            const WindowRecord& rec) {
  return rec.width_px != s.width_px || rec.height_px != s.height_px ||
         rec.dpi_scale_percent != s.dpi;
}

// Mark the session stopped, signal the worker, clear its queue. Internal
// state only (no adapter/encoder calls) — the manager lock is held by
// the caller in every use.
void CaptureSessionManager::stopSessionLocked(Session& s,
                                              CaptureSessionState next) {
  if (s.state == CaptureSessionState::Running ||
      s.state == CaptureSessionState::Paused) {
    s.state = next;
    s.metrics.state = next;
    s.stop_worker.store(true);
    s.gate.discard();
    s.worker_cv.notify_all();
  } else if (s.state == CaptureSessionState::Ended && !s.worker_joined) {
    // Worker may still be draining after a SourceClosed; stop it.
    s.stop_worker.store(true);
    s.gate.discard();
    s.worker_cv.notify_all();
  }
}

// Join the worker and release the encoder. teardown_mu held; the manager
// lock is NOT held (the worker's last callbacks take it).
void CaptureSessionManager::joinAndRelease(Session& s) {
  if (s.worker.joinable() && !s.worker_joined) s.worker.join();
  s.worker_joined = true;
  if (s.encoder) {
    s.encoder->stop();
    s.encoder.reset();
  }
}

// ---------------------------------------------------------------- queries

bool CaptureSessionManager::isShared(const std::string& window_id) const {
  std::lock_guard<std::mutex> mu(impl_->mu);
  return impl_->sessions.find(window_id) != impl_->sessions.end();
}

CaptureSessionState CaptureSessionManager::stateOf(
    const std::string& window_id) const {
  std::lock_guard<std::mutex> mu(impl_->mu);
  auto it = impl_->sessions.find(window_id);
  return it == impl_->sessions.end() ? CaptureSessionState::Inactive
                                     : it->second->state;
}

CaptureSessionMetrics CaptureSessionManager::metrics(
    const std::string& window_id) const {
  std::lock_guard<std::mutex> mu(impl_->mu);
  auto it = impl_->sessions.find(window_id);
  if (it == impl_->sessions.end()) return CaptureSessionMetrics{};
  CaptureSessionMetrics m = it->second->metrics;
  m.queue_depth = it->second->gate.depth();
  m.queue_max_depth = it->second->gate.maxDepth();
  return m;
}

std::vector<std::string> CaptureSessionManager::sharedWindowIds() const {
  std::lock_guard<std::mutex> mu(impl_->mu);
  std::vector<std::string> out;
  for (const auto& [id, _] : impl_->sessions) out.push_back(id);
  return out;
}

size_t CaptureSessionManager::sharedCount() const {
  std::lock_guard<std::mutex> mu(impl_->mu);
  return impl_->sessions.size();
}

size_t CaptureSessionManager::runningSurfaceCount() const {
  std::vector<std::string> running;
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    for (const auto& [_, s] : impl_->sessions)
      if (s->surface_started) running.push_back(s->window_id);
  }
  size_t n = 0;
  for (const std::string& id : running)
    if (impl_->source.isRunning(id)) ++n;
  return n;
}

// ------------------------------------------------------------- app layer

CaptureError CaptureSessionManager::shareWindow(const WindowRecord& record) {
  if (record.window_id.empty() ||
      record.surface_kind != WindowSurfaceKind::IndependentTopLevel) {
    // Child controls live inside the parent surface — a capture session
    // would duplicate pixels (FR-03: no duplicate stream).
    return CaptureError::SourceUnavailable;
  }
  return startSession(record, /*auto_shared=*/false, nowUs());
}

CaptureError CaptureSessionManager::unshareWindow(const std::string& window_id) {
  std::lock_guard<std::mutex> td(impl_->teardown_mu);
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto it = impl_->sessions.find(window_id);
    if (it == impl_->sessions.end()) return CaptureError::None;
    it->second->state = CaptureSessionState::Inactive;
    it->second->metrics.state = CaptureSessionState::Inactive;
    it->second->stop_worker.store(true);
    it->second->gate.discard();
    it->second->worker_cv.notify_all();
  }
  // Adapter call outside the manager lock (the adapter's stop path may
  // fire onSourceError, which takes the lock). Idempotent by contract
  // when the surface is not running.
  impl_->source.stopSurface(window_id);
  // Join the worker OUTSIDE the manager lock (its last callbacks take
  // it), then erase the entry.
  Session* sp = nullptr;
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto it = impl_->sessions.find(window_id);
    if (it != impl_->sessions.end()) sp = it->second.get();
  }
  if (sp) {
    sp->surface_started = false;
    joinAndRelease(*sp);
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto it = impl_->sessions.find(window_id);
    if (it != impl_->sessions.end()) impl_->sessions.erase(it);
  }
  return CaptureError::None;
}

// ------------------------------------------------------ start / recreate

CaptureError CaptureSessionManager::startSession(const WindowRecord& record,
                                                 bool auto_shared,
                                                 uint64_t now_us) {
  const CaptureManagerConfig& cfg = impl_->config;
  std::lock_guard<std::mutex> td(impl_->teardown_mu);

  Session* s = nullptr;
  bool need_stop = false;  // existing surface to tear down first
  CaptureSessionState entry_state = CaptureSessionState::Inactive;
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto& map = impl_->sessions;
    auto it = map.find(record.window_id);
    if (it == map.end()) {
      auto sess = std::make_unique<Session>();
      sess->gate.reset(cfg.queue_depth);
      sess->window_id = record.window_id;
      it = map.emplace(record.window_id, std::move(sess)).first;
      s = it->second.get();
    } else {
      s = it->second.get();
      entry_state = s->state;
      if (s->state == CaptureSessionState::Running ||
          s->state == CaptureSessionState::Paused) {
        if (!geometryChanged(*s, record)) {
          // Idempotent re-share with unchanged geometry.
          s->minimized = record.minimized;
          s->metrics.minimized = record.minimized;
          return CaptureError::None;
        }
        need_stop = true;  // size change: recreation
      }
      // Always stop the previous worker before the join below: after a
      // SourceClosed start failure the entry stays (Ended) with its
      // worker still running.
      s->stop_worker.store(true);
      s->gate.discard();
      s->worker_cv.notify_all();
    }
  }

  if (need_stop) {
    std::lock_guard<std::mutex> mu(impl_->mu);
    stopSessionLocked(*s, CaptureSessionState::Inactive);
  }
  joinAndRelease(*s);
  if (need_stop) {
    // Release the adapter surface (outside the manager lock; the
    // adapter's stop path may fire onSourceError, which takes the lock).
    impl_->source.stopSurface(record.window_id);
  }

  // New encoder + sink for this (re)start (the encoder is created with
  // the per-session counting sink so every delivered frame updates the
  // session metrics).
  IVideoEncoder* enc = nullptr;
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    s->sink = std::make_unique<CountingSink>(*this, record.window_id);
    s->encoder = impl_->encoder_factory.create(record.window_id, *s->sink);
    enc = s->encoder.get();
  }

  bool encoder_ok = false;
  if (enc) {
    // Codec init outside the manager lock (a backend may push to the
    // sink from start()).
    encoder_ok = enc->start(record.width_px, record.height_px, cfg.fps_hint);
  }
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    if (!encoder_ok) {
      s->metrics.error_count++;
      s->state = CaptureSessionState::Inactive;
      s->metrics.state = CaptureSessionState::Inactive;
      s->encoder.reset();
      s->sink.reset();
    } else {
      // Record start-time fields (geometry, identity, app session).
      s->hwnd = record.hwnd;
      s->width_px = record.width_px;
      s->height_px = record.height_px;
      s->dpi = record.dpi_scale_percent;
      s->minimized = record.minimized;
      s->app_session_id = record.app_session_id;
      s->auto_shared = s->auto_shared || auto_shared;
      {
        std::lock_guard<std::mutex> lk(s->worker_mu);
        s->fps_gate.target_fps = cfg.minimize_low_fps;
        s->fps_gate.reset(now_us);
      }
      s->fps_active.store(record.minimized &&
                          cfg.minimize_policy == MinimizePolicy::LowFps);
      s->metrics.width_px = record.width_px;
      s->metrics.height_px = record.height_px;
      s->metrics.dpi_scale_percent = record.dpi_scale_percent;
      s->metrics.minimized = record.minimized;
      s->metrics.auto_shared = s->auto_shared;
      s->metrics.queue_max_depth = s->gate.maxDepth();
      // The coalesce window only starts after an actual recreation
      // (never after the very first start: the first size change must
      // always recreate).
      if (need_stop || entry_state == CaptureSessionState::Ended)
        s->metrics.last_recreate_ts_us = now_us;
      s->metrics.keyframe_requests++;  // IDR at start / after recreation
    }
  }
  if (!encoder_ok) {
    // No worker was started and the encoder was released above: remove
    // the entry entirely (a failed share leaves no shared session).
    std::lock_guard<std::mutex> mu(impl_->mu);
    impl_->sessions.erase(record.window_id);
    impl_->encoded_sink.onEncoderError(record.window_id, "start_failed");
    return CaptureError::InternalError;
  }
  enc->requestKeyframe();  // outside the lock (the sink may call back)

  // Worker (re)start: create AND assign under the manager lock so a
  // concurrent teardown can never miss the thread.
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    s->stop_worker.store(false);
    s->worker_joined = false;
    s->worker = std::thread([this, s] {
      for (;;) {
        {
          std::unique_lock<std::mutex> lk(s->worker_mu);
          s->worker_cv.wait(lk, [s] {
            return s->stop_worker.load() || s->gate.depth() > 0;
          });
        }
        if (s->stop_worker.load() && s->gate.depth() == 0) break;
        CapturedFrame frame;
        if (!s->gate.pop(frame)) {
          if (s->stop_worker.load()) break;
          continue;
        }
        if (s->stop_worker.load()) break;
        if (s->fps_active.load()) {
          bool pass;
          std::lock_guard<std::mutex> lk(s->worker_mu);
          pass = s->fps_gate.mayCapture(monotonicUs());
          if (!pass) {
            std::lock_guard<std::mutex> mu(impl_->mu);
            auto it = impl_->sessions.find(s->window_id);
            if (it != impl_->sessions.end())
              it->second->metrics.frames_dropped++;  // throttle drop
            continue;
          }
        }
        const bool ok = s->encoder && s->encoder->encodeFrame(frame);
        if (!ok) {
          std::lock_guard<std::mutex> mu(impl_->mu);
          auto it = impl_->sessions.find(s->window_id);
          if (it != impl_->sessions.end()) it->second->metrics.error_count++;
        }
      }
    });
  }

  // Surface start (outside the manager lock; the adapter's event path
  // calls onSourceError, which takes the lock). A window that is
  // minimized at share time with the Pause policy gets no surface yet:
  // WGC delivers no frames for a minimized window; restore starts it
  // (fresh frames + IDR).
  const bool start_surface = !(record.minimized &&
                               cfg.minimize_policy == MinimizePolicy::Pause);
  CaptureError err = CaptureError::None;
  if (start_surface) {
    CaptureSurfaceRequest req;
    req.window_id = record.window_id;
    req.hwnd = record.hwnd;
    req.width_px = record.width_px;
    req.height_px = record.height_px;
    req.dpi_scale_percent = record.dpi_scale_percent;
    err = impl_->source.startSurface(req, *this);
  }

  CaptureError start_err = CaptureError::None;
  bool failed_hard = false;  // non-SourceClosed start failure: erase entry
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    if (err != CaptureError::None) {
      s->metrics.error_count++;
      s->surface_started = false;
      if (err == CaptureError::SourceClosed) {
        // Keep the entry (Ended): the window may still be tracked and a
        // later notification retries the recreation path.
        s->state = CaptureSessionState::Ended;
        s->metrics.state = CaptureSessionState::Ended;
      } else {
        // No session: stop the worker, then join + erase below (outside
        // the manager lock — the worker's last callbacks take it).
        s->state = CaptureSessionState::Inactive;
        s->metrics.state = CaptureSessionState::Inactive;
        s->stop_worker.store(true);
        s->gate.discard();
        s->worker_cv.notify_all();
        failed_hard = true;
      }
    } else if (start_surface) {
      s->surface_started = true;
      s->state = CaptureSessionState::Running;
      s->metrics.state = CaptureSessionState::Running;
      s->metrics.surface_starts++;
    } else {
      s->surface_started = false;
      s->state = CaptureSessionState::Paused;
      s->metrics.state = CaptureSessionState::Paused;
      s->metrics.fps_target = 0;
    }
  }
  if (failed_hard) {
    joinAndRelease(*s);
    std::lock_guard<std::mutex> mu2(impl_->mu);
    impl_->sessions.erase(record.window_id);
  }
  return err;
}

// -------------------------------------------------------- registry policy

void CaptureSessionManager::onRegistryNotifications(
    const std::vector<RegistryNotification>& ns) {
  for (const auto& n : ns) onRegistryNotification(n);
}

void CaptureSessionManager::onRegistryNotification(const RegistryNotification& n) {
  const CaptureManagerConfig& cfg = impl_->config;
  const uint64_t now = nowUs();

  if (n.kind == RegistryNotificationKind::Remove) {
    // FR-04: release ONLY this session; the parent and all unrelated
    // streams keep running.
    unshareWindow(n.window_id);
    return;
  }

  const WindowRecord& rec = n.record;

  enum class Action { None, AutoStart, Recreate };
  Action action = Action::None;
  bool is_recreate_of_ended = false;
  bool need_pause_surface_stop = false;
  bool need_resume = false;
  bool auto_shared = false;

  {
    std::lock_guard<std::mutex> td(impl_->teardown_mu);
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto it = impl_->sessions.find(rec.window_id);
    if (n.kind == RegistryNotificationKind::Announce ||
        n.kind == RegistryNotificationKind::Reannounce) {
      if (!requiresOwnCaptureSurface(rec)) return;  // child control
      if (it == impl_->sessions.end()) {
        // FR-03 auto-share: an independently rendered surface of an
        // ALREADY-SHARED application session — any role, ANY pid
        // (session correlation comes from the registry, ADR-0006) — is
        // captured without any manual request.
        if (!rec.app_session_id.empty()) {
          for (const auto& [_, s] : impl_->sessions) {
            if (s->app_session_id == rec.app_session_id &&
                (s->state == CaptureSessionState::Running ||
                 s->state == CaptureSessionState::Paused)) {
              action = Action::AutoStart;
              break;
            }
          }
        }
      } else if (it->second->state == CaptureSessionState::Ended) {
        // Source died but the window is still tracked: recreation path
        // (IDR on restart), bounded by max_recreate_attempts.
        if (it->second->recreate_attempts < cfg.max_recreate_attempts) {
          it->second->recreate_attempts++;
          auto_shared = it->second->auto_shared;
          is_recreate_of_ended = true;
          action = Action::Recreate;
        }
      } else if (it->second->state == CaptureSessionState::Running ||
                 it->second->state == CaptureSessionState::Paused) {
        if (geometryChanged(*it->second, rec) &&
            now - it->second->metrics.last_recreate_ts_us >=
                static_cast<uint64_t>(cfg.recreate_coalesce_ms) * 1000) {
          auto_shared = it->second->auto_shared;
          action = Action::Recreate;
        }
      }
    } else if (n.kind == RegistryNotificationKind::Update) {
      if (it == impl_->sessions.end()) return;
      Session& s = *it->second;
      if (s.state == CaptureSessionState::Inactive) return;
      if (s.state == CaptureSessionState::Ended) {
        // Source died but the window is still tracked: recreation path
        // (IDR on restart), bounded by max_recreate_attempts.
        if (s.recreate_attempts < cfg.max_recreate_attempts) {
          s.recreate_attempts++;
          auto_shared = s.auto_shared;
          action = Action::Recreate;
        }
      } else {
      // Minimize transition (configurable: pause vs low-fps).
      if (rec.minimized != s.minimized) {
        if (rec.minimized) {
          s.minimized = true;
          s.metrics.minimized = true;
          if (cfg.minimize_policy == MinimizePolicy::Pause &&
              s.state == CaptureSessionState::Running) {
            s.state = CaptureSessionState::Paused;
            s.metrics.state = CaptureSessionState::Paused;
            s.metrics.fps_target = 0;
            s.surface_started = false;
            need_pause_surface_stop = true;
          } else if (cfg.minimize_policy == MinimizePolicy::LowFps &&
                     s.state == CaptureSessionState::Running) {
            std::lock_guard<std::mutex> lk(s.worker_mu);
            s.fps_gate.target_fps = cfg.minimize_low_fps;
            s.fps_gate.reset(now);
            s.fps_active.store(true);
            s.metrics.fps_target = cfg.minimize_low_fps;
          }
        } else {
          s.minimized = false;
          s.metrics.minimized = false;
          if (s.state == CaptureSessionState::Paused) {
            need_resume = true;  // fresh surface + fresh frame + IDR
          } else {
            std::lock_guard<std::mutex> lk(s.worker_mu);
            s.fps_gate.reset(now);
            s.fps_active.store(false);
            s.metrics.fps_target = 0;
          }
        }
      }
      // Size/dpi change: recreation (coalesced for interactive resize,
      // FR-06). Position-only moves never recreate (WGC tracks the
      // window); a resize while paused is remembered and applied on
      // restore.
      if (s.state == CaptureSessionState::Paused) {
        if (geometryChanged(s, rec)) {
          s.width_px = rec.width_px;
          s.height_px = rec.height_px;
          s.dpi = rec.dpi_scale_percent;
          s.metrics.width_px = rec.width_px;
          s.metrics.height_px = rec.height_px;
          s.metrics.dpi_scale_percent = rec.dpi_scale_percent;
        }
      } else if (geometryChanged(s, rec) &&
                 now - s.metrics.last_recreate_ts_us >=
                     static_cast<uint64_t>(cfg.recreate_coalesce_ms) * 1000) {
        auto_shared = s.auto_shared;
        action = Action::Recreate;
      }
      }  // else: not Ended
    }
  }

  if (need_pause_surface_stop) {
    // Idempotent adapter call outside the manager lock.
    impl_->source.stopSurface(rec.window_id);
  }

  if (action == Action::AutoStart || action == Action::Recreate) {
    // AutoStart: the auto-share policy started it (no manual request).
    // Recreate: keep the existing session's auto_shared flag.
    const bool as = (action == Action::AutoStart) ? true : auto_shared;
    startSession(rec, as, now);
  }

  if (need_resume) {
    // Restore: fresh surface + fresh frame request + IDR (FR-03: resume
    // with a fresh frame request).
    std::lock_guard<std::mutex> td(impl_->teardown_mu);
    bool do_start = false;
    CaptureSurfaceRequest req;
    {
      std::lock_guard<std::mutex> mu(impl_->mu);
      auto it = impl_->sessions.find(rec.window_id);
      if (it != impl_->sessions.end() &&
          it->second->state == CaptureSessionState::Paused &&
          it->second->encoder) {
        do_start = true;
        req.window_id = it->second->window_id;
        req.hwnd = it->second->hwnd;
        req.width_px = it->second->width_px;
        req.height_px = it->second->height_px;
        req.dpi_scale_percent = it->second->dpi;
      }
    }
    CaptureError err = CaptureError::None;
    if (do_start) err = impl_->source.startSurface(req, *this);
    IVideoEncoder* enc = nullptr;
    {
      std::lock_guard<std::mutex> mu(impl_->mu);
      auto it = impl_->sessions.find(rec.window_id);
      if (it == impl_->sessions.end()) return;
      Session& s = *it->second;
      if (err == CaptureError::None) {
        s.surface_started = true;
        s.state = CaptureSessionState::Running;
        s.metrics.state = CaptureSessionState::Running;
        s.metrics.surface_starts++;
        s.metrics.keyframe_requests++;  // IDR after resume
        {
          std::lock_guard<std::mutex> lk(s.worker_mu);
          s.fps_gate.reset(now);
        }
        s.fps_active.store(false);
        s.metrics.fps_target = 0;
        enc = s.encoder.get();
      } else {
        s.metrics.error_count++;  // stays Paused; the next restore retry
      }
    }
    if (enc) enc->requestKeyframe();  // outside the lock
  }
}

// ------------------------------------------------------- IFrameConsumer

void CaptureSessionManager::onCapturedFrame(const CapturedFrame& frame) {
  std::lock_guard<std::mutex> mu(impl_->mu);
  auto it = impl_->sessions.find(frame.window_id);
  if (it == impl_->sessions.end()) return;
  Session& s = *it->second;
  if (s.state != CaptureSessionState::Running) return;
  s.metrics.frames_received++;
  if (frame.timestamp_us > s.metrics.last_frame_ts_us)
    s.metrics.last_frame_ts_us = frame.timestamp_us;
  const uint64_t drops_before = s.gate.droppedCount();
  s.gate.submit(frame);
  s.metrics.frames_dropped += s.gate.droppedCount() - drops_before;
  s.worker_cv.notify_all();
}

void CaptureSessionManager::onSourceError(const std::string& window_id,
                                          CaptureError error) {
  const CaptureManagerConfig& cfg = impl_->config;
  WindowRecord restart_rec;
  bool restart = false;
  bool auto_shared = false;
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto it = impl_->sessions.find(window_id);
    if (it == impl_->sessions.end()) return;
    Session& s = *it->second;
    s.metrics.error_count++;
    if (error == CaptureError::SourceClosed &&
        (s.state == CaptureSessionState::Running ||
         s.state == CaptureSessionState::Paused)) {
      // The adapter's surface is gone; the session is marked Ended and
      // immediately recreated (recreation path: fresh surface + IDR)
      // while the window is still shared. Other sessions are untouched
      // (FR-04 isolation).
      s.surface_started = false;
      s.state = CaptureSessionState::Ended;
      s.metrics.state = CaptureSessionState::Ended;
      s.stop_worker.store(true);
      s.gate.discard();
      s.worker_cv.notify_all();
      if (s.recreate_attempts < cfg.max_recreate_attempts) {
        s.recreate_attempts++;
        restart = true;
        auto_shared = s.auto_shared;
        restart_rec.window_id = s.window_id;
        restart_rec.hwnd = s.hwnd;
        restart_rec.width_px = s.width_px;
        restart_rec.height_px = s.height_px;
        restart_rec.dpi_scale_percent = s.dpi;
        restart_rec.minimized = s.minimized;
        restart_rec.app_session_id = s.app_session_id;
        restart_rec.surface_kind = WindowSurfaceKind::IndependentTopLevel;
      }
    } else if (error == CaptureError::SourceUnavailable &&
               (s.state == CaptureSessionState::Running ||
                s.state == CaptureSessionState::Paused)) {
      // Permanent surface failure (OS consent revoked, capture
      // creation denied): the session is Ended WITHOUT a recreation
      // retry — retrying would spin against the refusal; re-sharing
      // requires a user action (consent) or a new explicit share.
      s.surface_started = false;
      s.state = CaptureSessionState::Ended;
      s.metrics.state = CaptureSessionState::Ended;
      s.stop_worker.store(true);
      s.gate.discard();
      s.worker_cv.notify_all();
    }
  }
  if (restart) startSession(restart_rec, auto_shared, nowUs());
}

// ------------------------------------------------------- encoded bookkeep

void CaptureSessionManager::noteEncoded(const std::string& window_id,
                                        const EncodedFrame& frame) {
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto it = impl_->sessions.find(window_id);
    if (it != impl_->sessions.end())
      it->second->metrics.frames_delivered++;
  }
  impl_->encoded_sink.onEncodedFrame(frame);  // outside the lock
}

void CaptureSessionManager::noteEncoderError(const std::string& window_id,
                                             const std::string& reason) {
  {
    std::lock_guard<std::mutex> mu(impl_->mu);
    auto it = impl_->sessions.find(window_id);
    if (it != impl_->sessions.end()) it->second->metrics.error_count++;
  }
  impl_->encoded_sink.onEncoderError(window_id, reason);  // outside the lock
}

}  // namespace remotewindows::core
