import Foundation

/// Foundation-only model behind `KeyboardShortcutsSettingsView`
/// (FR-08 8.2/8.3): owns the master toggle, per-built-in toggles, and
/// the custom-mapping CRUD, persisting through `Preferences`
/// (master) + `CustomShortcutStore` (custom rules + built-in
/// overrides) and keeping the `ShortcutAdaptationService` in sync.
///
/// All mutation returns typed results — validation failures are
/// surfaced to the UI via `localizedMessage(for:)`, never swallowed.
/// The AppKit view is a thin renderer of this model, so every setting
/// behavior is unit-testable without a display.
public final class KeyboardShortcutsSettingsModel {
    public let preferences: Preferences
    public let store: CustomShortcutStore
    public let catalog: BuiltInShortcutCatalog
    public let service: ShortcutAdaptationService
    public let validator: ShortcutRuleValidator

    /// Store load problems (future version / corrupt) that must be
    /// shown to the user instead of silently discarding or repairing
    /// data.
    public private(set) var storeLoadWarning: String?

    /// `store` must use the same `UserDefaults` domain as
    /// `preferences` (production: both `.standard`; tests inject a
    /// suite domain into both).
    public init(preferences: Preferences = .shared,
                store: CustomShortcutStore = CustomShortcutStore(),
                catalog: BuiltInShortcutCatalog = .standard) {
        self.preferences = preferences
        self.catalog = catalog
        self.store = store
        self.validator = ShortcutRuleValidator(catalog: catalog)
        self.service = ShortcutAdaptationService(
            catalog: catalog,
            masterEnabled: preferences.adaptShortcutsMaster,
            builtInOverrides: store.builtInOverrides())
        switch store.load() {
        case .loaded(let rules):
            service.updateCustomRules(rules)
        case .empty:
            break
        case .futureVersion(let stored):
            storeLoadWarning = String(
                format: L10n.t("shortcuts.store.futureVersion"), stored)
        case .corrupt(let reason):
            storeLoadWarning = String(
                format: L10n.t("shortcuts.store.corrupt"), reason)
        }
    }

    // MARK: Master toggle (FR-08 8.2 — disables all adaptation without
    // deleting saved rules)

    public var masterEnabled: Bool {
        get { preferences.adaptShortcutsMaster }
        set {
            preferences.adaptShortcutsMaster = newValue
            service.setMasterEnabled(newValue)
        }
    }

    // MARK: Built-in rows

    public struct BuiltInRow: Equatable {
        public let rule: ShortcutRule
        public let enabled: Bool
        /// Localized display name (`shortcuts.builtIn.<id>`).
        public let displayName: String
    }

    /// One row per built-in mapping, catalog order, with the
    /// user's effective toggle state (per-ID override, else default).
    public var builtInRows: [BuiltInRow] {
        catalog.rules.map { rule in
            BuiltInRow(rule: rule,
                       enabled: service.isBuiltInEnabled(id: rule.id),
                       displayName: validator.displayName(forID: rule.id))
        }
    }

    public func setBuiltInEnabled(id: String, enabled: Bool) {
        store.setBuiltInOverride(id: id, enabled: enabled)
        service.setBuiltInEnabled(id: id, enabled: enabled)
    }

    // MARK: Custom mappings (CRUD + reorder + enable/disable)

    public var customRules: [ShortcutRule] { service.customRules }

    /// Creates a custom rule. On success the rule is persisted (user
    /// order appended) and the service updated; on failure NOTHING is
    /// changed and the typed error is returned for display.
    @discardableResult
    public func createCustom(label: String, source: SourceChord,
                             target: TargetChord, enabled: Bool,
                             scope: ShortcutScope = .allApplications)
        -> Result<ShortcutRule, ShortcutValidationError> {
        let rule = ShortcutRule(id: Self.newCustomID(), kind: .custom,
                                source: source, target: target,
                                label: label, enabled: enabled, scope: scope)
        var candidates = service.customRules
        candidates.append(rule)
        guard case .success = validator.validate(
            rule, otherCustom: candidates,
            enabledBuiltInIDs: enabledBuiltInIDs()) else {
            return .failure(validationError(for: rule, in: candidates))
        }
        persistCustom(candidates)
        return .success(rule)
    }

    /// Edits an existing custom rule (source/target/label/scope/
    /// enabled). Re-validated against the other rules; on failure
    /// NOTHING changes.
    @discardableResult
    public func updateCustom(id: String, label: String,
                             source: SourceChord, target: TargetChord,
                             enabled: Bool, scope: ShortcutScope)
        -> Result<ShortcutRule, ShortcutValidationError> {
        guard let index = service.customRules.firstIndex(where: { $0.id == id })
        else { return .failure(.emptyTrigger) }
        var candidates = service.customRules
        candidates[index] = ShortcutRule(
            id: id, kind: .custom, source: source, target: target,
            label: label, enabled: enabled, scope: scope)
        guard case .success = validator.validate(
            candidates[index], otherCustom: candidates,
            enabledBuiltInIDs: enabledBuiltInIDs()) else {
            return .failure(validationError(for: candidates[index],
                                            in: candidates))
        }
        persistCustom(candidates)
        return .success(candidates[index])
    }

    public func deleteCustom(id: String) {
        persistCustom(service.customRules.filter { $0.id != id })
    }

    /// `direction` -1 = up, +1 = down. Out-of-range moves are no-ops.
    public func moveCustom(id: String, direction: Int) {
        let rules = service.customRules
        guard let index = rules.firstIndex(where: { $0.id == id }) else { return }
        let target = index + direction
        guard target >= 0, target < rules.count else { return }
        var moved = rules
        let rule = moved.remove(at: index)
        moved.insert(rule, at: target)
        persistCustom(moved)
    }

    /// Enabling is re-validated (a previously-conflicting rule may now
    /// be enabled); disabling always succeeds. On failure NOTHING
    /// changes and the error is returned for display.
    @discardableResult
    public func setCustomEnabled(id: String, enabled: Bool)
        -> Result<Void, ShortcutValidationError> {
        guard let index = service.customRules.firstIndex(where: { $0.id == id })
        else { return .failure(.emptyTrigger) }
        if enabled && service.customRules[index].enabled { return .success(()) }
        var candidates = service.customRules
        candidates[index].enabled = enabled
        if enabled {
            guard case .success = validator.validate(
                candidates[index], otherCustom: candidates,
                enabledBuiltInIDs: enabledBuiltInIDs()) else {
                return .failure(validationError(for: candidates[index],
                                                in: candidates))
            }
        }
        persistCustom(candidates)
        return .success(())
    }

    // MARK: Error display

    /// The user-facing message for a validation error. Duplicate
    /// sources show the CONFLICTING rule's display name (custom: the
    /// user's label, else its ID; built-in: the localized catalog
    /// name) — never a silent resolution.
    public func localizedMessage(for error: ShortcutValidationError) -> String {
        guard case .duplicateSource(let ruleID) = error else {
            return error.localizedMessage
        }
        let name: String
        if let custom = service.customRules.first(where: { $0.id == ruleID }),
           !custom.label.isEmpty {
            name = custom.label
        } else {
            name = validator.displayName(forID: ruleID)
        }
        return String(format: L10n.t("shortcuts.validation.duplicateSource"), name)
    }

    // MARK: Helpers

    private func enabledBuiltInIDs() -> Set<String> {
        Set(catalog.rules.filter { service.isBuiltInEnabled(id: $0.id) }.map(\.id))
    }

    private func validationError(for rule: ShortcutRule, in candidates: [ShortcutRule])
        -> ShortcutValidationError {
        switch validator.validate(rule, otherCustom: candidates,
                                  enabledBuiltInIDs: enabledBuiltInIDs()) {
        case .success:
            return .emptyTrigger // unreachable: only called on failure
        case .failure(let error):
            return error
        }
    }

    private func persistCustom(_ rules: [ShortcutRule]) {
        store.save(rules)
        service.updateCustomRules(rules)
    }

    private static func newCustomID() -> String {
        "custom-\(UUID().uuidString.lowercased())"
    }
}
