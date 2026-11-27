import Foundation

/// The public shortcut-adaptation seam (FR-08 8.4).
///
/// INTEGRATION SEAM (for the later input-pipeline stage — requires
/// C-WINDOWS window tracking + H-INPUT per the plan's dependency gate;
/// this task deliberately wires NO key forwarding):
///
/// The future `InputRouter` calls `translate(_:)` for every macOS key
/// event ONLY while a remote window is the active input target, and:
///
/// - `.adapted(events)` → emit `events` as protocol `input_event`
///   `key` frames, in order, and emit nothing else for the source event;
/// - `.passthrough`     → forward the event through the baseline
///   key/text translation path (`KeyboardEventTranslator.baseline`);
/// - `.consumed`        → emit nothing: the event belongs to an
///   in-flight adapted chord whose target-side events already went out
///   inside the `.adapted` sequence;
///
/// and calls `reset()` on focus loss and on disconnect (FR-07: "on
/// disconnect, reset the input state so a key or button cannot remain
/// logically pressed").
///
/// Matching (FR-08 8.1): PHYSICAL key codes + exact normalized modifier
/// masks — never localized characters. Priority (plan §6.2): enabled
/// custom rules in user order, then enabled built-ins in catalog
/// order; first match wins. Deterministic and fully unit-tested; no I/O.
public final class ShortcutAdaptationService {
    // MARK: Rule state

    public private(set) var masterEnabled: Bool
    private let catalog: BuiltInShortcutCatalog
    private var builtInOverrides: [String: Bool]
    private var storedCustomRules: [ShortcutRule]

    // MARK: Tracked pressed-key state

    /// The matched chord currently being held: its rule + the source
    /// keys (the key code and every physical modifier key implied by the
    /// source mask) still physically down. Key-ups of these keys are
    /// `.consumed` until the set empties. Cleared by `reset()` (focus
    /// loss / disconnect) and when the last source key is released.
    private struct InFlightChord {
        let rule: ShortcutRule
        var remainingSourceKeys: Set<UInt16>
    }
    private var inFlight: InFlightChord?

    /// Test/inspection surface for the tracked pressed-key state.
    public var inFlightSourceKeys: Set<UInt16> {
        inFlight?.remainingSourceKeys ?? []
    }

    public init(catalog: BuiltInShortcutCatalog = .standard,
                masterEnabled: Bool = true,
                builtInOverrides: [String: Bool] = [:],
                customRules: [ShortcutRule] = []) {
        self.catalog = catalog
        self.masterEnabled = masterEnabled
        self.builtInOverrides = builtInOverrides
        self.storedCustomRules = customRules.filter { $0.kind == .custom }
    }

    // MARK: Rule management (driven by the settings UI / model)

    public func setMasterEnabled(_ on: Bool) {
        masterEnabled = on
        // Disabling adaptation mid-chord cannot leave keys consumed.
        if !on { inFlight = nil }
    }

    public func setBuiltInEnabled(id: String, enabled: Bool) {
        builtInOverrides[id] = enabled
    }

    public func isBuiltInEnabled(id: String) -> Bool {
        builtInOverrides[id] ?? catalog.rule(id: id)?.enabled ?? false
    }

    public func updateCustomRules(_ rules: [ShortcutRule]) {
        storedCustomRules = rules.filter { $0.kind == .custom }
    }

    public var customRules: [ShortcutRule] { storedCustomRules }

    /// The enabled rules in MATCH PRIORITY order: custom (user order)
    /// first, then built-ins (catalog order). Plan §6.2.
    public var activeRules: [ShortcutRule] {
        var out = customRules.filter { $0.enabled }
        for rule in catalog.rules where isBuiltInEnabled(id: rule.id) {
            out.append(rule)
        }
        return out
    }

    // MARK: The seam

    /// Evaluate one macOS key event against the active rules.
    public func translate(_ event: KeyPressEvent) -> TranslationDecision {
        if !masterEnabled {
            inFlight = nil
            return .passthrough
        }
        let mods = event.macModifiers

        if let flight = inFlight {
            if !event.isDown {
                if flight.remainingSourceKeys.contains(event.keyCode) {
                    // Releasing a key releases its whole logical group:
                    // if the chord owns both physical Cmd variants,
                    // releasing either one clears both — the chord must
                    // be completable with one physical key per modifier.
                    var remaining = flight.remainingSourceKeys
                    for variant in Self.logicalKeyGroup(of: event.keyCode) {
                        remaining.remove(variant)
                    }
                    inFlight = remaining.isEmpty
                        ? nil
                        : InFlightChord(rule: flight.rule,
                                        remainingSourceKeys: remaining)
                    return .consumed
                }
                return .passthrough
            }
            // A repeat of a key still held inside the matched chord
            // re-fires the target combination (FR-08 8.1 "preserve
            // key-repeat behavior where appropriate").
            if flight.remainingSourceKeys.contains(event.keyCode),
               mods == flight.rule.source.macModifiers {
                return .adapted(KeyboardEventTranslator.sequence(for: flight.rule.target))
            }
            return .passthrough
        }

        guard event.isDown else { return .passthrough }
        for rule in activeRules where matches(rule, event: event, mods: mods) {
            inFlight = InFlightChord(rule: rule,
                                     remainingSourceKeys: Self.sourceKeys(of: rule.source))
            return .adapted(KeyboardEventTranslator.sequence(for: rule.target))
        }
        return .passthrough
    }

    /// Release/reset all tracked key state (focus loss, disconnect).
    /// Idempotent; a subsequent `translate` behaves like a fresh service.
    public func reset() {
        inFlight = nil
    }

    // MARK: Matching

    private func matches(_ rule: ShortcutRule, event: KeyPressEvent,
                         mods: UInt32) -> Bool {
        guard rule.enabled, let key = rule.source.keyCode,
              key == event.keyCode, rule.source.macModifiers == mods else {
            return false
        }
        return true
    }

    /// The physical keys an adapted chord "owns": its key code plus
    /// both possible physical variants of each source modifier (left
    /// and right Shift/Ctrl/Option/Cmd), so releasing either variant is
    /// consumed rather than forwarded as a stray Windows key-up.
    static func sourceKeys(of source: SourceChord) -> Set<UInt16> {
        var keys: Set<UInt16> = []
        if let key = source.keyCode { keys.insert(key) }
        if source.macModifiers & MacModifier.shift != 0 {
            keys.insert(MacKey.shift); keys.insert(MacKey.rightShift)
        }
        if source.macModifiers & MacModifier.control != 0 {
            keys.insert(MacKey.control); keys.insert(MacKey.rightControl)
        }
        if source.macModifiers & MacModifier.option != 0 {
            keys.insert(MacKey.option); keys.insert(MacKey.rightOption)
        }
        if source.macModifiers & MacModifier.command != 0 {
            keys.insert(MacKey.command); keys.insert(MacKey.rightCommand)
        }
        return keys
    }

    /// All physical variants of one logical key (a modifier maps to its
    /// left+right pair; any other key maps to itself). Used so releasing
    /// either Cmd variant completes the chord.
    static func logicalKeyGroup(of keyCode: UInt16) -> Set<UInt16> {
        switch keyCode {
        case MacKey.shift, MacKey.rightShift:
            return [MacKey.shift, MacKey.rightShift]
        case MacKey.control, MacKey.rightControl:
            return [MacKey.control, MacKey.rightControl]
        case MacKey.option, MacKey.rightOption:
            return [MacKey.option, MacKey.rightOption]
        case MacKey.command, MacKey.rightCommand:
            return [MacKey.command, MacKey.rightCommand]
        default:
            return [keyCode]
        }
    }
}
