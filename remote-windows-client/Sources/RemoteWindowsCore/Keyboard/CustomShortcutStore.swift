import Foundation

/// Versioned persistence for CUSTOM shortcut rules and built-in
/// toggle overrides (FR-08 8.3).
///
/// Client-only: rules are NEVER sent to the Windows Host (no protocol
/// change). Two independent storage slots so a future catalog revision
/// can add or revise built-in defaults without overwriting user state:
///
/// - `keyboard.customRules`      — the user's custom rule list under a
///   versioned schema (`version` + `rules` array);
/// - `keyboard.builtInOverrides` — per-ID built-in enable overrides
///   (`[id: Bool]`), separate from the custom list.
///
/// The master toggle lives on `Preferences.adaptShortcutsMaster`
/// (shipped with RW-C-001) and is deliberately not duplicated here.
///
/// Honest failure: loading a FUTURE schema version (or corrupt data)
/// leaves the stored bytes untouched and reports the problem — the
/// store never rewrites, "repairs", or drops user data it does not
/// understand.
///
/// Test seam: inject a non-standard `UserDefaults` (e.g. a suiteName
/// domain); production uses `.standard`.
public final class CustomShortcutStore {
    /// Bump only when the custom-rule schema changes shape.
    public static let schemaVersion = 1

    private static let customRulesKey = "keyboard.customRules"
    private static let builtInOverridesKey = "keyboard.builtInOverrides"

    private let defaults: UserDefaults
    private let encoder: JSONEncoder
    private let decoder: JSONDecoder

    public init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        self.encoder = encoder
        let decoder = JSONDecoder()
        self.decoder = decoder
    }

    // MARK: Custom rules

    private struct Schema: Codable {
        var version: Int
        var rules: [ShortcutRule]
    }

    public enum LoadOutcome: Equatable {
        case loaded([ShortcutRule])
        case empty
        /// The stored blob declares a version newer than this build
        /// understands. Data left untouched.
        case futureVersion(stored: Int)
        /// The stored blob is not valid JSON / schema. Data left
        /// untouched.
        case corrupt(reason: String)
    }

    public func load() -> LoadOutcome {
        guard let data = defaults.data(forKey: Self.customRulesKey) else {
            return .empty
        }
        guard let decoded = try? decoder.decode(Schema.self, from: data) else {
            return .corrupt(reason: "not decodable as the custom-rule schema")
        }
        guard decoded.version <= Self.schemaVersion else {
            return .futureVersion(stored: decoded.version)
        }
        // Only custom rules ever live in this slot; anything else is
        // ignored (the slot is owned by this store).
        return .loaded(decoded.rules.filter { $0.kind == .custom })
    }

    /// Saves the full custom rule list (user order IS significant —
    /// it is the match priority). Schema version is written as-is;
    /// saving never bumps the version.
    @discardableResult
    public func save(_ rules: [ShortcutRule]) -> Bool {
        let schema = Schema(version: Self.schemaVersion,
                            rules: rules.filter { $0.kind == .custom })
        guard let data = try? encoder.encode(schema) else { return false }
        defaults.set(data, forKey: Self.customRulesKey)
        return true
    }

    // MARK: Built-in enable overrides (separately persisted)

    public func builtInOverrides() -> [String: Bool] {
        defaults.dictionary(forKey: Self.builtInOverridesKey) as? [String: Bool]
            ?? [:]
    }

    public func setBuiltInOverride(id: String, enabled: Bool) {
        var overrides = builtInOverrides()
        overrides[id] = enabled
        defaults.set(overrides, forKey: Self.builtInOverridesKey)
    }

    public func clearBuiltInOverride(id: String) {
        var overrides = builtInOverrides()
        overrides[id] = nil
        defaults.set(overrides, forKey: Self.builtInOverridesKey)
    }
}
