import Foundation

/// Centralized localization access (plan rule 15, FR-14).
///
/// All user-facing strings go through `L10n.t(_:)` with a stable key.
/// The MVP ships English only (`Resources/Localizable.en.json`); adding a
/// locale later means adding a `Localizable.<locale>.json` resource — no
/// business-logic changes. Keys are language-neutral identifiers and are
/// never shown verbatim except as a last-resort fallback in a missing-key
/// bug (which tests catch).
public enum L10n {
    public static func t(_ key: String) -> String {
        table[key] ?? key
    }

    /// All shipped keys (test surface for the localization audit).
    public static var allKeys: [String] { Array(table.keys).sorted() }

    private static let table: [String: String] = load()

    private static func load() -> [String: String] {
        let candidates = [
            Bundle.module.url(forResource: "Localizable", withExtension: "json",
                              subdirectory: nil, localization: "en"),
            Bundle.module.url(forResource: "Localizable.en", withExtension: "json",
                              subdirectory: nil, localization: nil),
            Bundle.module.url(forResource: "Localizable", withExtension: "en.json",
                              subdirectory: nil, localization: nil),
        ]
        for url in candidates {
            guard let url else { continue }
            if let data = try? Data(contentsOf: url),
               let obj = try? JSONSerialization.jsonObject(with: data) as? [String: String] {
                return obj
            }
        }
        return [:]
    }
}
