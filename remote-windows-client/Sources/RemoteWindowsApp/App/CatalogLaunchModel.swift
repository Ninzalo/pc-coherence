import Foundation
import RemoteWindowsCore

/// Per-app launch lifecycle phase (task RW-C-002, FR-02).
///
/// `idle` → (launch clicked) → `launching` → `started` | `failed`;
/// host `app_status` events refine or reset the phase:
/// - `running` → `running` (host-observed liveness, heartbeat-piggybacked);
/// - `closed` → back to `idle` (the app can be relaunched) — except a
///   `failed` phase keeps its safe message so the user still sees why the
///   last attempt failed.
/// The client shows exactly what the host reports: `started` stays
/// `started` until the host says more, and `window_detected` is never
/// invented by the client.
public enum CatalogLaunchPhase: Equatable {
    case idle
    case launching
    /// Host reported the process started (launch_result `started`).
    case started(sessionID: String?)
    /// Host reported the app is running (app_status `running`).
    case running(sessionID: String)
    case failed(safeMessage: String)
}

/// In-memory catalog + per-app launch state (RW-C-002).
///
/// The Windows Host is the source of truth: the catalog is replaced
/// wholesale on every `app_catalog`, launch state follows the host's
/// `launch_result`/`app_status`, and everything resets on disconnect —
/// nothing is persisted (task packet, Out of scope). Depends only on
/// RemoteWindowsCore (no AppKit): the AppKit UI reads this model on the
/// main queue (callbacks arrive on the transport queue; the UI hops to
/// main).
public final class CatalogLaunchModel {

    /// The current catalog entries, as delivered by the host.
    public private(set) var entries: [AppEntry] = []
    public private(set) var catalogRevision: UInt32 = 0
    /// Case-insensitive substring filter on `display_name` (empty = all).
    public var searchText: String = ""

    private var launchStates: [String: CatalogLaunchPhase] = [:]

    public init() {}

    // MARK: Catalog

    /// Replace the catalog wholesale (host `app_catalog`). Launch state
    /// for apps the catalog no longer lists is dropped; surviving apps
    /// keep their lifecycle state (the host keeps tracking their
    /// sessions across catalog revisions).
    public func applyCatalog(_ catalog: AppCatalog) {
        entries = catalog.entries
        catalogRevision = catalog.catalogRevision
        let known = Set(entries.map { $0.appID })
        launchStates = launchStates.filter { known.contains($0.key) }
    }

    /// Drop all catalog + launch state (session disconnected: no stale
    /// "Running" after the socket dies).
    public func reset() {
        entries = []
        catalogRevision = 0
        launchStates = [:]
    }

    public func entry(for appID: String) -> AppEntry? {
        entries.first { $0.appID == appID }
    }

    // MARK: Launch lifecycle

    public func launchState(for appID: String) -> CatalogLaunchPhase {
        launchStates[appID] ?? .idle
    }

    /// A new `launch_app` for the app: from any phase → `launching`.
    public func beginLaunch(appID: String) {
        launchStates[appID] = .launching
    }

    /// Host `launch_result`. `accepted`/`starting` stay `launching` until
    /// the host says `started` (process start is one reported event — no
    /// client-side invention). `failed` keeps the host's safe message
    /// (falling back to the machine error code, which is safe to show).
    public func applyLaunchResult(_ result: LaunchResult) {
        switch result.state {
        case .accepted, .starting:
            launchStates[result.appID] = .launching
        case .started, .windowDetected:
            launchStates[result.appID] = .started(sessionID: result.applicationSessionID)
        case .failed:
            let message = result.safeMessage.isEmpty ? result.errorCode : result.safeMessage
            launchStates[result.appID] = .failed(safeMessage: message)
        default:
            break
        }
    }

    /// Host `app_status` (heartbeat-piggybacked liveness). `running` is
    /// host-observed and supersedes `started` for display; `closed`
    /// returns the app to `idle` — except a `failed` launch keeps its
    /// message.
    public func applyAppStatus(_ status: AppStatus) {
        switch status.state {
        case .running:
            launchStates[status.appID] = .running(sessionID: status.applicationSessionID)
        case .closed:
            if case .failed = launchStates[status.appID] ?? .idle {
                break // failed keeps its message
            }
            launchStates[status.appID] = .idle
        default:
            break
        }
    }

    // MARK: Display helpers

    /// Entries matching `searchText` (case-insensitive substring on
    /// `display_name`), sorted by `display_name` (localized compare).
    public var visibleEntries: [AppEntry] {
        let query = searchText.trimmingCharacters(in: .whitespaces).lowercased()
        let sorted = entries.sorted {
            $0.displayName.localizedCompare($1.displayName) == .orderedAscending
        }
        guard !query.isEmpty else { return sorted }
        return sorted.filter { $0.displayName.lowercased().contains(query) }
    }
}
