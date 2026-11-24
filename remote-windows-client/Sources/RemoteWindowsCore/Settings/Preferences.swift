import Foundation

/// Versioned macOS Client preferences (FR-08.3, FR-09).
///
/// Stored in UserDefaults; nothing sensitive (pairing secrets belong in the
/// Keychain — see docs/SECURITY.md). `settingsSchemaVersion` lets future
/// releases distinguish built-in defaults from user edits so an update can
/// add or revise defaults without overwriting saved rules.
public final class Preferences {
    public static let shared = Preferences()

    /// Bump when the settings model changes shape; user values are kept.
    public static let settingsSchemaVersion = 1

    private let defaults: UserDefaults

    public init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
    }

    // MARK: Connection (FR-10: manual host address in the MVP)

    public var hostAddress: String {
        get { defaults.string(forKey: Keys.hostAddress) ?? "127.0.0.1" }
        set { defaults.set(newValue, forKey: Keys.hostAddress) }
    }

    public var hostPort: Int {
        get { defaults.object(forKey: Keys.hostPort) as? Int ?? 47987 }
        set { defaults.set(newValue, forKey: Keys.hostPort) }
    }

    /// Product runtime port per docs/SECURITY.md (LAN only).
    public static let defaultHostPort = 47987

    // MARK: Frame rate (FR-09)

    /// Inactive-window FPS ceiling. Allowed range 1...60, default 10.
    public var inactiveFps: Int {
        get { defaults.object(forKey: Keys.inactiveFps) as? Int ?? 10 }
        set {
            let clamped = min(max(newValue, 1), 60)
            defaults.set(clamped, forKey: Keys.inactiveFps)
        }
    }

    /// Minimized windows: `true` = pause transmission (default),
    /// `false` = use `inactiveFps` instead (FR-09).
    public var pauseMinimizedStreams: Bool {
        get { defaults.object(forKey: Keys.pauseMinimized) as? Bool ?? true }
        set { defaults.set(newValue, forKey: Keys.pauseMinimized) }
    }

    // MARK: Shortcut adaptation (FR-08.2)

    /// Master toggle: disables all built-in and custom adaptation without
    /// deleting saved rules.
    public var adaptShortcutsMaster: Bool {
        get { defaults.object(forKey: Keys.adaptMaster) as? Bool ?? true }
        set { defaults.set(newValue, forKey: Keys.adaptMaster) }
    }

    private enum Keys {
        static let hostAddress = "connection.hostAddress"
        static let hostPort = "connection.hostPort"
        static let inactiveFps = "performance.inactiveFps"
        static let pauseMinimized = "performance.pauseMinimizedStreams"
        static let adaptMaster = "keyboard.adaptShortcutsMaster"
    }
}
