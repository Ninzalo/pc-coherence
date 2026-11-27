import Foundation

/// Typed, user-facing validation errors for shortcut rules
/// (FR-08 8.3: "show a clear validation message rather than silently
/// selecting an arbitrary rule"). Each error maps to exactly one L10n
/// key (English-only MVP, FR-14 / plan rule 15); rule IDs and key
/// codes stay machine identifiers inside the message, never localized
/// identifiers.
public enum ShortcutValidationError: Error, Equatable {
    /// Source chord has neither a key nor modifiers.
    case emptyTrigger
    /// Source chord has modifiers but no key.
    case modifierOnlyTrigger
    /// Target has no key (windowsVk == 0).
    case emptyTarget
    /// Target VK is outside the supported target domain (unknown VK,
    /// out of the contract 1...0xFF range, or a modifier VK used as the
    /// main key).
    case unsupportedTarget(vk: UInt32)
    /// Source key code is not a known macOS physical key.
    case unsupportedSource(key: UInt16)
    /// An ENABLED rule with the same source chord already exists
    /// (custom vs custom, or custom vs an enabled built-in). The
    /// associated value is the conflicting rule's ID.
    case duplicateSource(ruleID: String)

    public var l10nKey: String {
        switch self {
        case .emptyTrigger: return "shortcuts.validation.emptyTrigger"
        case .modifierOnlyTrigger: return "shortcuts.validation.modifierOnlyTrigger"
        case .emptyTarget: return "shortcuts.validation.emptyTarget"
        case .unsupportedTarget: return "shortcuts.validation.unsupportedTarget"
        case .unsupportedSource: return "shortcuts.validation.unsupportedSource"
        case .duplicateSource: return "shortcuts.validation.duplicateSource"
        }
    }

    /// Optional format argument for the duplicate-source message
    /// (the conflicting rule's display name).
    public var l10nArgument: String? {
        switch self {
        case .duplicateSource(let ruleID): return ruleID
        default: return nil
        }
    }

    /// The localized message the settings UI must display. Never
    /// swallowed: the UI shows this verbatim.
    public var localizedMessage: String {
        guard let arg = l10nArgument else { return L10n.t(l10nKey) }
        return String(format: L10n.t(l10nKey), arg)
    }
}

/// Rule validation (FR-08 8.3). Pure and deterministic: given a rule,
/// the other custom rules, and which built-in IDs are enabled, it
/// returns either success or the FIRST failing check in a fixed order
/// (empty → modifier-only → target → source → duplicate).
public struct ShortcutRuleValidator {
    public let catalog: BuiltInShortcutCatalog

    public init(catalog: BuiltInShortcutCatalog) {
        self.catalog = catalog
    }

    /// Validates `rule` (custom rules; built-ins are catalog-owned and
    /// shipped valid) against the rest of the rule set.
    ///
    /// Duplicate-source rule (plan §6.2): a conflict is flagged when
    /// `rule` is enabled AND another ENABLED rule — custom or built-in
    /// — has the same source chord. In the MVP every scope is global,
    /// so scopes always overlap; per-application scoping is deferred
    /// (documented in the settings UI). Disabled rules cannot fire, so
    /// they never conflict.
    public func validate(_ rule: ShortcutRule,
                         otherCustom: [ShortcutRule] = [],
                         enabledBuiltInIDs: Set<String> = [])
        -> Result<Void, ShortcutValidationError> {

        if rule.source.keyCode == nil && rule.source.macModifiers == 0 {
            return .failure(.emptyTrigger)
        }
        if rule.source.keyCode == nil {
            return .failure(.modifierOnlyTrigger)
        }
        if rule.target.isEmpty {
            return .failure(.emptyTarget)
        }
        if !(1...0xFF).contains(rule.target.windowsVk)
            || !WinVk.supportedTargetVk.contains(rule.target.windowsVk) {
            return .failure(.unsupportedTarget(vk: rule.target.windowsVk))
        }
        if let key = rule.source.keyCode, !MacKey.knownKeyCodes.contains(key) {
            return .failure(.unsupportedSource(key: key))
        }
        if rule.enabled {
            for other in otherCustom
            where other.id != rule.id
                && other.enabled
                && other.source == rule.source {
                return .failure(.duplicateSource(ruleID: other.id))
            }
            for builtIn in catalog.rules
            where builtIn.enabled
                && enabledBuiltInIDs.contains(builtIn.id)
                && builtIn.source == rule.source {
                return .failure(.duplicateSource(ruleID: builtIn.id))
            }
        }
        return .success(())
    }

    /// The display name for a rule ID: built-ins resolve through their
    /// L10n key (`shortcuts.builtIn.<id>`), customs fall back to the
    /// raw ID (the UI substitutes the user label when it has one).
    public func displayName(forID id: String) -> String {
        if id.hasPrefix("custom-") { return id }
        let key = "shortcuts.builtIn.\(id)"
        return L10n.t(key) == key ? id : L10n.t(key)
    }
}
