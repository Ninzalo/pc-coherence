import Foundation
@testable import RemoteWindowsCore

/// RW-C-004 (C-INPUT): FR-08 shortcut adaptation stack.
///
/// Deterministic fixtures only — no display, no live input pipeline,
/// no wire emission. Covers: the exact 16-rule built-in table, matching
/// + custom-over-built-in precedence, emission ordering (pinned literal
/// event arrays), validator rejections (never silent), store versioning
/// + honest-failure, master/per-rule toggles, pressed-key state +
/// reset(), unmatched-typing baseline, repeat behavior, and the L10n
/// keys the settings UI uses.
enum ShortcutAdaptationSuite {
    /// `validator.validate` returns `Result<Void, ShortcutValidationError>`;
    /// Void is not Equatable, so assert the failure case by pattern.
    static func expectValidationError(
        _ actual: Result<Void, ShortcutValidationError>,
        _ expected: ShortcutValidationError,
        _ message: String = "") {
        switch actual {
        case .success:
            harness.expect(false,
                           message.isEmpty
                           ? "expected failure \(expected), got success" : message)
        case .failure(let error):
            harness.expectEqual(error, expected, message)
        }
    }

    static let cases: [Harness.Case] = [
        .case("builtInCatalogExactTable") {
            let catalog = BuiltInShortcutCatalog.standard
            harness.expectEqual(catalog.rules.count, 16, "FR-08 8.2: exactly 16 built-in rules")
            // id -> (macKeyCode, macModifiers, windowsVk, winModifiers, defaultEnabled)
            let expected: [(String, UInt16, UInt32, UInt32, UInt32, Bool)] = [
                ("copy",     MacKey.c, MacModifier.command, WinVk.c, WinMod.control, true),
                ("cut",      MacKey.x, MacModifier.command, WinVk.x, WinMod.control, true),
                ("paste",    MacKey.v, MacModifier.command, WinVk.v, WinMod.control, true),
                ("selectAll", MacKey.a, MacModifier.command, WinVk.a, WinMod.control, true),
                ("undo",     MacKey.z, MacModifier.command, WinVk.z, WinMod.control, true),
                ("redo",     MacKey.z, MacModifier.command | MacModifier.shift, WinVk.y, WinMod.control, true),
                ("find",     MacKey.f, MacModifier.command, WinVk.f, WinMod.control, true),
                ("save",     MacKey.s, MacModifier.command, WinVk.s, WinMod.control, true),
                ("open",     MacKey.o, MacModifier.command, WinVk.o, WinMod.control, true),
                ("newDoc",   MacKey.n, MacModifier.command, WinVk.n, WinMod.control, true),
                ("print",    MacKey.p, MacModifier.command, WinVk.p, WinMod.control, true),
                ("closeDoc", MacKey.w, MacModifier.command, WinVk.w, WinMod.control, true),
                ("quitApp",  MacKey.q, MacModifier.command, WinVk.f4, WinMod.alt, false),
                ("refresh",  MacKey.r, MacModifier.command, WinVk.r, WinMod.control, false),
                ("newTab",   MacKey.t, MacModifier.command, WinVk.t, WinMod.control, false),
                ("saveAs",   MacKey.s, MacModifier.command | MacModifier.shift, WinVk.s, WinMod.control | WinMod.shift, false),
            ]
            let ids = Set(catalog.rules.map(\.id))
            for (id, key, mods, vk, winMods, enabled) in expected {
                guard let rule = catalog.rule(id: id) else {
                    _ = try harness.fail("built-in rule missing: \(id)")
                }
                harness.expectEqual(rule.kind, .builtIn, "id \(id)")
                harness.expectEqual(rule.source.keyCode, key, "id \(id): source keyCode")
                harness.expectEqual(rule.source.macModifiers, mods, "id \(id): source modifiers")
                harness.expectEqual(rule.target.windowsVk, vk, "id \(id): target VK")
                harness.expectEqual(rule.target.winModifiers, winMods, "id \(id): target modifiers")
                harness.expectEqual(rule.enabled, enabled, "id \(id): default enabled")
                harness.expectEqual(rule.scope, ShortcutScope.allApplications, "id \(id): global scope")
            }
            harness.expectEqual(ids.count, 16, "rule IDs are unique")
        },

        .case("builtInTargetsInsideContractDomain") {
            for rule in BuiltInShortcutCatalog.standard.rules {
                harness.expect((1...0xFF).contains(rule.target.windowsVk),
                               "\(rule.id): VK in contract domain 1...0xFF")
                harness.expect(rule.target.winModifiers <= 0xF,
                               "\(rule.id): WIN_MOD bits within 0...0xF")
                harness.expect(WinVk.supportedTargetVk.contains(rule.target.windowsVk),
                               "\(rule.id): VK is a supported target key")
            }
        },

        .case("eachEnabledBuiltInMatchesOnPhysicalCodeAndFlags") {
            // A fresh service per rule: each match starts an in-flight
            // chord, and a new chord must not evaluate against the
            // previous one's tracked state.
            for rule in BuiltInShortcutCatalog.standard.rules
            where rule.enabled {
                let service = ShortcutAdaptationService()
                let event = KeyPressEvent.down(rule.source.keyCode!, rule.source.macModifiers)
                let expected: TranslationDecision =
                    .adapted(KeyboardEventTranslator.sequence(for: rule.target))
                harness.expectEqual(service.translate(event), expected,
                                    "built-in \(rule.id) must match")
            }
        },

        .case("defaultDisabledBuiltInsDoNotMatch") {
            let service = ShortcutAdaptationService()
            for id in ["quitApp", "refresh", "newTab", "saveAs"] {
                let rule = BuiltInShortcutCatalog.standard.rule(id: id)!
                let event = KeyPressEvent.down(rule.source.keyCode!, rule.source.macModifiers)
                harness.expectEqual(service.translate(event), .passthrough,
                                    "\(id) is off by default")
            }
        },

        .case("unknownTriggerPassthrough") {
            let service = ShortcutAdaptationService()
            // ⌘K: no rule. Bare key: no rule. Key-up: never matches.
            harness.expectEqual(service.translate(KeyPressEvent.down(MacKey.k, MacModifier.command)),
                                .passthrough)
            harness.expectEqual(service.translate(KeyPressEvent.down(MacKey.a, 0)), .passthrough)
            harness.expectEqual(service.translate(KeyPressEvent.up(MacKey.a, 0)), .passthrough)
            harness.expectEqual(service.translate(KeyPressEvent.up(MacKey.c, MacModifier.command)),
                                .passthrough)
        },

        .case("exactModifierMatching") {
            let service = ShortcutAdaptationService()
            let copyRule = BuiltInShortcutCatalog.standard.rule(id: "copy")!
            let copySeq: TranslationDecision =
                .adapted(KeyboardEventTranslator.sequence(for: copyRule.target))
            // Extra Shift → different chord: no match.
            harness.expectEqual(
                service.translate(KeyPressEvent.down(MacKey.c, MacModifier.command | MacModifier.shift)),
                .passthrough)
            // Wrong modifier (Ctrl instead of Cmd): no match.
            harness.expectEqual(
                service.translate(KeyPressEvent.down(MacKey.c, MacModifier.control)),
                .passthrough)
            // Exact match still adapts (fresh service: the previous
            // passthroughs tracked nothing).
            let fresh = ShortcutAdaptationService()
            harness.expectEqual(fresh.translate(KeyPressEvent.down(MacKey.c, MacModifier.command)),
                                copySeq)
        },

        .case("customOverridesBuiltIn") {
            // Custom ⌘C → Ctrl+V beats the built-in copy (⌘C → Ctrl+C),
            // per FR-08 8.1: "A matching custom rule takes precedence
            // over a built-in rule".
            let custom = ShortcutRule(
                id: "custom-test", kind: .custom,
                source: SourceChord(keyCode: MacKey.c, macModifiers: MacModifier.command),
                target: TargetChord(windowsVk: WinVk.v, winModifiers: WinMod.control),
                label: "test override", enabled: true)
            let service = ShortcutAdaptationService(customRules: [custom])
            let expected: TranslationDecision =
                .adapted(KeyboardEventTranslator.sequence(for: custom.target))
            harness.expectEqual(
                service.translate(KeyPressEvent.down(MacKey.c, MacModifier.command)),
                expected, "custom rule must win over built-in copy")
        },

        .case("disabledCustomDoesNotShadowBuiltIn") {
            let disabled = ShortcutRule(
                id: "custom-test", kind: .custom,
                source: SourceChord(keyCode: MacKey.c, macModifiers: MacModifier.command),
                target: TargetChord(windowsVk: WinVk.v, winModifiers: WinMod.control),
                label: "disabled override", enabled: false)
            let service = ShortcutAdaptationService(customRules: [disabled])
            let copyRule = BuiltInShortcutCatalog.standard.rule(id: "copy")!
            let expected: TranslationDecision =
                .adapted(KeyboardEventTranslator.sequence(for: copyRule.target))
            harness.expectEqual(
                service.translate(KeyPressEvent.down(MacKey.c, MacModifier.command)),
                expected, "disabled custom must not shadow the built-in")
        },

        .case("masterOffPassthroughWithoutDeletingRules") {
            let custom = ShortcutRule(
                id: "custom-test", kind: .custom,
                source: SourceChord(keyCode: MacKey.k, macModifiers: MacModifier.command),
                target: TargetChord(windowsVk: WinVk.l, winModifiers: WinMod.control),
                label: "k", enabled: true)
            let service = ShortcutAdaptationService(customRules: [custom])
            service.setMasterEnabled(false)
            harness.expectEqual(
                service.translate(KeyPressEvent.down(MacKey.c, MacModifier.command)),
                .passthrough, "master off: built-in trigger")
            harness.expectEqual(
                service.translate(KeyPressEvent.down(MacKey.k, MacModifier.command)),
                .passthrough, "master off: custom trigger")
            // Stored rules survive: re-enabling master restores them.
            // (12 built-ins are enabled by default per the plan table.)
            harness.expectEqual(service.activeRules.count, 13,
                                "12 enabled built-ins + 1 custom remain stored")
            service.setMasterEnabled(true)
            let kRule = service.customRules[0]
            harness.expectEqual(
                service.translate(KeyPressEvent.down(MacKey.k, MacModifier.command)),
                .adapted(KeyboardEventTranslator.sequence(for: kRule.target)))
        },

        .case("emissionOrderMultiModifierSaveAs") {
            // saveAs target: Ctrl+Shift+S. Pinned literal sequence:
            // modifier-downs before the key (Shift then Ctrl — fixed
            // order), key-up, modifier-ups in REVERSE.
            let target = TargetChord(windowsVk: WinVk.s,
                                     winModifiers: WinMod.control | WinMod.shift)
            let expected: [KeyInput] = [
                KeyInput(windowsVk: WinVk.shift, modifierBits: 0x1, keyDown: true),
                KeyInput(windowsVk: WinVk.control, modifierBits: 0x3, keyDown: true),
                KeyInput(windowsVk: WinVk.s, modifierBits: 0x3, keyDown: true),
                KeyInput(windowsVk: WinVk.s, modifierBits: 0x3, keyDown: false),
                KeyInput(windowsVk: WinVk.control, modifierBits: 0x1, keyDown: false),
                KeyInput(windowsVk: WinVk.shift, modifierBits: 0x0, keyDown: false),
            ]
            harness.expectEqual(KeyboardEventTranslator.sequence(for: target), expected)
        },

        .case("emissionOrderAltF4QuitApp") {
            // quitApp target: Alt+F4.
            let target = TargetChord(windowsVk: WinVk.f4, winModifiers: WinMod.alt)
            let expected: [KeyInput] = [
                KeyInput(windowsVk: WinVk.menu, modifierBits: 0x4, keyDown: true),
                KeyInput(windowsVk: WinVk.f4, modifierBits: 0x4, keyDown: true),
                KeyInput(windowsVk: WinVk.f4, modifierBits: 0x4, keyDown: false),
                KeyInput(windowsVk: WinVk.menu, modifierBits: 0x0, keyDown: false),
            ]
            harness.expectEqual(KeyboardEventTranslator.sequence(for: target), expected)
        },

        .case("emissionOrderSingleModifierCopy") {
            // copy target: Ctrl+C.
            let target = TargetChord(windowsVk: WinVk.c, winModifiers: WinMod.control)
            let expected: [KeyInput] = [
                KeyInput(windowsVk: WinVk.control, modifierBits: 0x2, keyDown: true),
                KeyInput(windowsVk: WinVk.c, modifierBits: 0x2, keyDown: true),
                KeyInput(windowsVk: WinVk.c, modifierBits: 0x2, keyDown: false),
                KeyInput(windowsVk: WinVk.control, modifierBits: 0x0, keyDown: false),
            ]
            harness.expectEqual(KeyboardEventTranslator.sequence(for: target), expected)
        },

        .case("emissionPairingInvariant") {
            // For every rule (built-in + no-modifier custom), every
            // key-down is paired with a matching key-up, and the
            // sequence shape is [mods↓..., key↓, key↑, mods↑...].
            var targets = BuiltInShortcutCatalog.standard.rules.map(\.target)
            targets.append(TargetChord(windowsVk: WinVk.a, winModifiers: 0))
            targets.append(TargetChord(windowsVk: WinVk.f5,
                                       winModifiers: WinMod.shift | WinMod.win))
            for target in targets {
                let seq = KeyboardEventTranslator.sequence(for: target)
                let downs = seq.filter(\.keyDown).count
                let ups = seq.filter { !$0.keyDown }.count
                harness.expectEqual(downs, ups, "paired downs/ups for \(target)")
                let modCount = [WinMod.shift, WinMod.control, WinMod.alt, WinMod.win]
                    .filter { target.winModifiers & $0 != 0 }.count
                harness.expectEqual(seq.count, 2 + 2 * modCount,
                                    "sequence length for \(target)")
                // Key events sit exactly in the middle.
                let midDown = seq[modCount]
                let midUp = seq[modCount + 1]
                harness.expectEqual(midDown.windowsVk, target.windowsVk)
                harness.expectEqual(midUp.windowsVk, target.windowsVk)
                harness.expect(midDown.keyDown && !midUp.keyDown, "key middle pair")
                // Final event always ends with zero modifiers.
                harness.expectEqual(seq.last!.modifierBits, 0,
                                    "all modifiers released for \(target)")
                // Contract domain on every emitted event.
                for e in seq {
                    harness.expect((1...0xFF).contains(e.windowsVk) && e.modifierBits <= 0xF,
                                   "contract domain for \(target)")
                }
            }
        },

        .case("chordKeysConsumedUntilReleased") {
            let service = ShortcutAdaptationService()
            let copyRule = BuiltInShortcutCatalog.standard.rule(id: "copy")!
            let seq = KeyboardEventTranslator.sequence(for: copyRule.target)
            harness.expectEqual(
                service.translate(KeyPressEvent.down(MacKey.c, MacModifier.command)),
                .adapted(seq))
            // The chord owns C + both physical Cmd keys.
            harness.expectEqual(service.inFlightSourceKeys,
                                Set([MacKey.c, MacKey.command, MacKey.rightCommand]))
            // Releasing C and Cmd is CONSUMED (their target-side
            // counterparts already went out in the adapted sequence) —
            // never forwarded as stray Windows key-ups.
            harness.expectEqual(
                service.translate(KeyPressEvent.up(MacKey.c, MacModifier.command)),
                .consumed)
            harness.expectEqual(
                service.translate(KeyPressEvent.up(MacKey.rightCommand, MacModifier.command)),
                .consumed)
            harness.expectEqual(service.inFlightSourceKeys, Set<UInt16>())
            // After full release, a bare C types normally again.
            harness.expectEqual(service.translate(KeyPressEvent.down(MacKey.a, 0)),
                                .passthrough)
        },

        .case("resetOnFocusLossClearsTrackedState") {
            let service = ShortcutAdaptationService()
            let copyRule = BuiltInShortcutCatalog.standard.rule(id: "copy")!
            _ = service.translate(KeyPressEvent.down(MacKey.c, MacModifier.command))
            harness.expect(!service.inFlightSourceKeys.isEmpty, "chord in flight")
            // Focus loss: reset. The pending physical key-ups must no
            // longer be consumed — the baseline path sees them.
            service.reset()
            harness.expectEqual(service.inFlightSourceKeys, Set<UInt16>())
            harness.expectEqual(
                service.translate(KeyPressEvent.up(MacKey.c, MacModifier.command)),
                .passthrough)
            harness.expectEqual(
                service.translate(KeyPressEvent.up(MacKey.command, MacModifier.command)),
                .passthrough)
            // ...and the service behaves fresh afterwards.
            harness.expectEqual(
                service.translate(KeyPressEvent.down(MacKey.c, MacModifier.command)),
                .adapted(KeyboardEventTranslator.sequence(for: copyRule.target)))
        },

        .case("resetIsIdempotent") {
            let service = ShortcutAdaptationService()
            service.reset()
            service.reset()
            harness.expectEqual(service.inFlightSourceKeys, Set<UInt16>())
            harness.expectEqual(service.translate(KeyPressEvent.up(MacKey.c, MacModifier.command)),
                                .passthrough)
        },

        .case("repeatWhileInFlightRefiresTarget") {
            let service = ShortcutAdaptationService()
            let copyRule = BuiltInShortcutCatalog.standard.rule(id: "copy")!
            let seq = KeyboardEventTranslator.sequence(for: copyRule.target)
            let first = service.translate(KeyPressEvent.down(MacKey.c, MacModifier.command))
            harness.expectEqual(first, .adapted(seq))
            // Auto-repeat of C while Cmd is held: the target combo
            // re-fires (FR-08 8.1 "preserve key-repeat behavior").
            let repeated = service.translate(
                KeyPressEvent.down(MacKey.c, MacModifier.command, isRepeat: true))
            harness.expectEqual(repeated, .adapted(seq),
                                "repeat re-fires the matched target")
            // The chord is still in flight: release consumes.
            harness.expectEqual(
                service.translate(KeyPressEvent.up(MacKey.c, MacModifier.command)),
                .consumed)
            harness.expectEqual(
                service.translate(KeyPressEvent.up(MacKey.command, MacModifier.command)),
                .consumed)
        },

        .case("repeatAfterModifierChangeMatches") {
            // C typed bare first (baseline), then Cmd added while C is
            // held: the auto-repeat event now carries ⌘ and matches.
            let service = ShortcutAdaptationService()
            let copyRule = BuiltInShortcutCatalog.standard.rule(id: "copy")!
            harness.expectEqual(
                service.translate(KeyPressEvent.down(MacKey.c, 0)), .passthrough)
            harness.expectEqual(
                service.translate(KeyPressEvent.down(MacKey.c, MacModifier.command, isRepeat: true)),
                .adapted(KeyboardEventTranslator.sequence(for: copyRule.target)))
        },

        .case("unmatchedTypingUsesBaselinePath") {
            // No rule matched → baseline translation (the path the
            // future pipeline forwards); no clipboard, no dropping.
            let plain = KeyPressEvent.down(MacKey.h, 0)
            harness.expectEqual(KeyboardEventTranslator.baseline(plain),
                                KeyInput(windowsVk: WinVk.h, modifierBits: 0,
                                         keyDown: true))
            let withCmd = KeyPressEvent.down(MacKey.e, MacModifier.command)
            harness.expectEqual(KeyboardEventTranslator.baseline(withCmd),
                                KeyInput(windowsVk: WinVk.e, modifierBits: WinMod.win,
                                         keyDown: true))
            let shift = KeyPressEvent.down(MacKey.f, MacModifier.shift)
            harness.expectEqual(KeyboardEventTranslator.baseline(shift),
                                KeyInput(windowsVk: WinVk.f, modifierBits: WinMod.shift,
                                         keyDown: true))
            let opt = KeyPressEvent.up(MacKey.l, MacModifier.option)
            harness.expectEqual(KeyboardEventTranslator.baseline(opt),
                                KeyInput(windowsVk: WinVk.l, modifierBits: WinMod.alt,
                                         keyDown: false))
            // Modifier key itself: no baseline VK (left to the text path).
            harness.expect(KeyboardEventTranslator.baseline(
                KeyPressEvent.down(MacKey.command, 0)) == nil,
                "Cmd has no baseline VK")
            // F4 maps (⌘F4 is the macOS "close window" key).
            harness.expectEqual(
                KeyboardEventTranslator.baseline(KeyPressEvent.down(MacKey.f4, 0)),
                KeyInput(windowsVk: WinVk.f4, modifierBits: 0, keyDown: true))
        },

        .case("validatorRejectsEmptyAndUnsupportedChords") {
            let validator = ShortcutRuleValidator(catalog: .standard)
            let okSource = SourceChord(keyCode: MacKey.k, macModifiers: MacModifier.command)
            let okTarget = TargetChord(windowsVk: WinVk.l, winModifiers: WinMod.control)
            func rule(_ source: SourceChord, _ target: TargetChord,
                      enabled: Bool = true) -> ShortcutRule {
                ShortcutRule(id: "custom-x", kind: .custom, source: source,
                             target: target, label: "x", enabled: enabled)
            }
            expectValidationError(
                validator.validate(rule(SourceChord(keyCode: nil, macModifiers: 0), okTarget)),
                .emptyTrigger)
            expectValidationError(
                validator.validate(rule(SourceChord(keyCode: nil, macModifiers: MacModifier.command),
                                        okTarget)),
                .modifierOnlyTrigger)
            expectValidationError(
                validator.validate(rule(okSource, TargetChord(windowsVk: 0, winModifiers: 0))),
                .emptyTarget)
            expectValidationError(
                validator.validate(rule(okSource, TargetChord(windowsVk: 0x80, winModifiers: 0))),
                .unsupportedTarget(vk: 0x80))
            expectValidationError(
                validator.validate(rule(okSource, TargetChord(windowsVk: 0x200, winModifiers: 0))),
                .unsupportedTarget(vk: 0x200))
            // A modifier VK is never a valid MAIN target key.
            expectValidationError(
                validator.validate(rule(okSource,
                                        TargetChord(windowsVk: WinVk.control,
                                                    winModifiers: 0))),
                .unsupportedTarget(vk: WinVk.control))
            expectValidationError(
                validator.validate(rule(SourceChord(keyCode: 0x99, macModifiers: 0), okTarget)),
                .unsupportedSource(key: 0x99))
            // A valid rule passes (⌘K is free: no built-in, no customs).
            switch validator.validate(rule(okSource, okTarget)) {
            case .success:
                break
            case .failure(let error):
                _ = try harness.fail("valid rule rejected: \(error)")
            }
        },

        .case("validatorRejectsDuplicateSourcesExplicitly") {
            let validator = ShortcutRuleValidator(catalog: .standard)
            let copy = BuiltInShortcutCatalog.standard.rule(id: "copy")!
            let otherCustom = ShortcutRule(
                id: "custom-other", kind: .custom,
                source: copy.source,
                target: TargetChord(windowsVk: WinVk.v, winModifiers: WinMod.control),
                label: "other", enabled: true)
            let dupe = ShortcutRule(
                id: "custom-dupe", kind: .custom,
                source: copy.source,
                target: TargetChord(windowsVk: WinVk.b, winModifiers: WinMod.control),
                label: "dupe", enabled: true)
            // Enabled custom vs ENABLED built-in (copy is on by default;
            // no other custom rules in this scenario).
            expectValidationError(
                validator.validate(dupe, otherCustom: [],
                                   enabledBuiltInIDs: ["copy"]),
                .duplicateSource(ruleID: "copy"))
            // Custom vs custom (the other custom is enabled).
            let dupe2 = ShortcutRule(
                id: "custom-dupe2", kind: .custom, source: copy.source,
                target: TargetChord(windowsVk: WinVk.m, winModifiers: WinMod.control),
                label: "dupe2", enabled: true)
            expectValidationError(
                validator.validate(dupe2, otherCustom: [otherCustom],
                                   enabledBuiltInIDs: []),
                .duplicateSource(ruleID: "custom-other"))
            // Built-in DISABLED by the user → no conflict.
            switch validator.validate(dupe, otherCustom: [], enabledBuiltInIDs: []) {
            case .success:
                break
            case .failure(let error):
                _ = try harness.fail("disabled built-in must not conflict, got \(error)")
            }
            // The new rule DISABLED → it cannot fire, no conflict.
            let dupeDisabled = ShortcutRule(
                id: "custom-dupe", kind: .custom,
                source: copy.source,
                target: TargetChord(windowsVk: WinVk.b, winModifiers: WinMod.control),
                label: "dupe", enabled: false)
            switch validator.validate(dupeDisabled, otherCustom: [],
                                      enabledBuiltInIDs: ["copy"]) {
            case .success:
                break
            case .failure(let error):
                _ = try harness.fail("disabled rule must not conflict, got \(error)")
            }
            // The conflicting error names the other rule.
            if case .failure(.duplicateSource(let id)) =
                validator.validate(dupe, otherCustom: [],
                                   enabledBuiltInIDs: ["copy"]) {
                harness.expectEqual(id, "copy")
            }
        },

        .case("storeRoundTrip") {
            let suite = "rwh-client-tests-\(UUID().uuidString)"
            let defaults = UserDefaults(suiteName: suite)!
            defaults.removePersistentDomain(forName: suite)
            defer { defaults.removePersistentDomain(forName: suite) }

            func custom(_ id: String, _ vk: UInt32, enabled: Bool = true) -> ShortcutRule {
                ShortcutRule(id: id, kind: .custom,
                             source: SourceChord(keyCode: MacKey.k,
                                                 macModifiers: MacModifier.command | MacModifier.shift),
                             target: TargetChord(windowsVk: vk, winModifiers: WinMod.control),
                             label: id, enabled: enabled)
            }
            let a = custom("custom-a", WinVk.l)
            let b = custom("custom-b", WinVk.m, enabled: false)
            let store = CustomShortcutStore(defaults: defaults)
            harness.expectEqual(store.load(), .empty)
            harness.expect(store.save([a, b]))
            let reloaded = CustomShortcutStore(defaults: defaults).load()
            harness.expectEqual(reloaded, .loaded([a, b]),
                                "save → load round-trip preserves rules + order")
            // Built-in rules can never be persisted through this slot.
            let builtIn = BuiltInShortcutCatalog.standard.rule(id: "copy")!
            _ = store.save([a, builtIn])
            harness.expectEqual(CustomShortcutStore(defaults: defaults).load(),
                                .loaded([a]),
                                "built-in rules are filtered out of the custom slot")
        },

        .case("storeFutureVersionLeftUntouched") {
            let suite = "rwh-client-tests-\(UUID().uuidString)"
            let defaults = UserDefaults(suiteName: suite)!
            defaults.removePersistentDomain(forName: suite)
            defer { defaults.removePersistentDomain(forName: suite) }
            let future = #"{"rules":[],"version":2}"#.data(using: .utf8)!
            defaults.set(future, forKey: "keyboard.customRules")
            let outcome = CustomShortcutStore(defaults: defaults).load()
            harness.expectEqual(outcome, .futureVersion(stored: 2))
            // Stored bytes untouched (honest failure, no rewrite).
            harness.expectEqual(defaults.data(forKey: "keyboard.customRules"), future)
        },

        .case("storeCorruptDataLeftUntouched") {
            let suite = "rwh-client-tests-\(UUID().uuidString)"
            let defaults = UserDefaults(suiteName: suite)!
            defaults.removePersistentDomain(forName: suite)
            defer { defaults.removePersistentDomain(forName: suite) }
            let garbage = "not json at all".data(using: .utf8)!
            defaults.set(garbage, forKey: "keyboard.customRules")
            let outcome = CustomShortcutStore(defaults: defaults).load()
            switch outcome {
            case .corrupt(let reason):
                harness.expect(!reason.isEmpty, "corrupt reason reported")
            default:
                try harness.fail("expected .corrupt, got \(outcome)")
            }
            harness.expectEqual(defaults.data(forKey: "keyboard.customRules"), garbage)
        },

        .case("builtInOverridesPersistSeparatelyFromCustomRules") {
            let suite = "rwh-client-tests-\(UUID().uuidString)"
            let defaults = UserDefaults(suiteName: suite)!
            defaults.removePersistentDomain(forName: suite)
            defer { defaults.removePersistentDomain(forName: suite) }
            let custom = ShortcutRule(
                id: "custom-a", kind: .custom,
                source: SourceChord(keyCode: MacKey.k, macModifiers: MacModifier.command),
                target: TargetChord(windowsVk: WinVk.l, winModifiers: WinMod.control),
                label: "k", enabled: true)
            let store = CustomShortcutStore(defaults: defaults)
            store.save([custom])
            let customBlobBefore = defaults.data(forKey: "keyboard.customRules")
            // Toggling a built-in must NOT touch the custom list bytes.
            store.setBuiltInOverride(id: "copy", enabled: false)
            store.setBuiltInOverride(id: "saveAs", enabled: true)
            harness.expectEqual(defaults.data(forKey: "keyboard.customRules"),
                                customBlobBefore,
                                "built-in overrides stored in a separate slot")
            let fresh = CustomShortcutStore(defaults: defaults)
            harness.expectEqual(fresh.builtInOverrides(),
                                ["copy": false, "saveAs": true])
            harness.expectEqual(fresh.load(), .loaded([custom]))
            // Master toggle lives on Preferences (RW-C-001 slot).
            let prefs = Preferences(defaults: defaults)
            prefs.adaptShortcutsMaster = false
            harness.expect(!Preferences(defaults: defaults).adaptShortcutsMaster)
        },

        .case("settingsModelMasterAndBuiltInToggles") {
            let suite = "rwh-client-tests-\(UUID().uuidString)"
            let defaults = UserDefaults(suiteName: suite)!
            defaults.removePersistentDomain(forName: suite)
            defer { defaults.removePersistentDomain(forName: suite) }
            let model = KeyboardShortcutsSettingsModel(
                preferences: Preferences(defaults: defaults),
                store: CustomShortcutStore(defaults: defaults))
            let copyEvent = KeyPressEvent.down(MacKey.c, MacModifier.command)
            let copyRule = BuiltInShortcutCatalog.standard.rule(id: "copy")!
            let pasteRule = BuiltInShortcutCatalog.standard.rule(id: "paste")!
            // Per-rule toggle: copy off, paste still adapts.
            model.setBuiltInEnabled(id: "copy", enabled: false)
            harness.expectEqual(model.service.translate(copyEvent), .passthrough,
                                "copy toggled off")
            harness.expectEqual(
                model.service.translate(KeyPressEvent.down(MacKey.v, MacModifier.command)),
                .adapted(KeyboardEventTranslator.sequence(for: pasteRule.target)))
            // Master off: everything passthrough, rules intact.
            model.masterEnabled = false
            harness.expectEqual(model.service.translate(copyEvent), .passthrough)
            harness.expectEqual(model.builtInRows.count, 16, "rows survive master off")
            harness.expectEqual(model.service.activeRules.count, 11,
                                "12 default-on built-ins minus copy")
            // Everything persisted across a fresh model instance.
            let again = KeyboardShortcutsSettingsModel(
                preferences: Preferences(defaults: defaults),
                store: CustomShortcutStore(defaults: defaults))
            harness.expect(!again.masterEnabled)
            harness.expect(!again.service.isBuiltInEnabled(id: "copy"))
            harness.expect(again.service.isBuiltInEnabled(id: "paste"))
            _ = copyRule
        },

        .case("settingsModelCustomCrudReorderPersistence") {
            let suite = "rwh-client-tests-\(UUID().uuidString)"
            let defaults = UserDefaults(suiteName: suite)!
            defaults.removePersistentDomain(forName: suite)
            defer { defaults.removePersistentDomain(forName: suite) }
            let model = KeyboardShortcutsSettingsModel(
                preferences: Preferences(defaults: defaults),
                store: CustomShortcutStore(defaults: defaults))
            // Create (⌥K and ⌃K: no built-in collision).
            let altK = SourceChord(keyCode: MacKey.k, macModifiers: MacModifier.option)
            let ctrlK = SourceChord(keyCode: MacKey.k, macModifiers: MacModifier.control)
            let r1 = try! model.createCustom(
                label: "first", source: altK,
                target: TargetChord(windowsVk: WinVk.l, winModifiers: WinMod.control),
                enabled: true).get()
            let r2 = try! model.createCustom(
                label: "second", source: ctrlK,
                target: TargetChord(windowsVk: WinVk.m, winModifiers: WinMod.control),
                enabled: true).get()
            harness.expectEqual(model.customRules.map(\.id), [r1.id, r2.id])
            // Duplicate source: explicit error, list unchanged.
            switch model.createCustom(
                label: "dupe", source: altK,
                target: TargetChord(windowsVk: WinVk.n, winModifiers: WinMod.control),
                enabled: true) {
            case .success:
                try harness.fail("duplicate custom source must be rejected")
            case .failure(let error):
                harness.expectEqual(error, .duplicateSource(ruleID: r1.id))
                harness.expect(!model.localizedMessage(for: error).isEmpty,
                               "conflict names the other rule")
            }
            harness.expectEqual(model.customRules.map(\.id), [r1.id, r2.id],
                                "rejected create changes nothing")
            // Reorder: r2 to the top.
            model.moveCustom(id: r2.id, direction: -1)
            harness.expectEqual(model.customRules.map(\.id), [r2.id, r1.id])
            // Out-of-range moves are no-ops.
            model.moveCustom(id: r2.id, direction: -1)
            harness.expectEqual(model.customRules.map(\.id), [r2.id, r1.id])
            // Edit r1's target.
            switch model.updateCustom(id: r1.id, label: "first edited", source: altK,
                                      target: TargetChord(windowsVk: WinVk.o,
                                                          winModifiers: WinMod.control),
                                      enabled: true, scope: .allApplications) {
            case .success:
                harness.expectEqual(model.customRules.first { $0.id == r1.id }?.target.windowsVk,
                                    WinVk.o)
            case .failure:
                try harness.fail("valid edit must succeed")
            }
            // Persistence round-trip through a fresh model.
            let again = KeyboardShortcutsSettingsModel(
                preferences: Preferences(defaults: defaults),
                store: CustomShortcutStore(defaults: defaults))
            harness.expectEqual(again.customRules.map(\.id), [r2.id, r1.id])
            // Delete.
            model.deleteCustom(id: r2.id)
            harness.expectEqual(model.customRules.map(\.id), [r1.id])
            let afterDelete = KeyboardShortcutsSettingsModel(
                preferences: Preferences(defaults: defaults),
                store: CustomShortcutStore(defaults: defaults))
            harness.expectEqual(afterDelete.customRules.map(\.id), [r1.id])
        },

        .case("settingsModelEnableConflictBlocksSilentlyNever") {
            let suite = "rwh-client-tests-\(UUID().uuidString)"
            let defaults = UserDefaults(suiteName: suite)!
            defaults.removePersistentDomain(forName: suite)
            defer { defaults.removePersistentDomain(forName: suite) }
            let model = KeyboardShortcutsSettingsModel(
                preferences: Preferences(defaults: defaults),
                store: CustomShortcutStore(defaults: defaults))
            // Disable built-in copy, then create a custom ⌘C.
            model.setBuiltInEnabled(id: "copy", enabled: false)
            let custom = try! model.createCustom(
                label: "my copy",
                source: SourceChord(keyCode: MacKey.c, macModifiers: MacModifier.command),
                target: TargetChord(windowsVk: WinVk.v, winModifiers: WinMod.control),
                enabled: true).get()
            harness.expect(model.service.isBuiltInEnabled(id: "copy") == false)
            // Re-enable the built-in. Now enabling a SECOND custom with
            // the same source must be rejected explicitly — the conflict
            // names the already-enabled custom (match priority order).
            model.setBuiltInEnabled(id: "copy", enabled: true)
            let dupe = try! model.createCustom(
                label: "dupe copy",
                source: SourceChord(keyCode: MacKey.c, macModifiers: MacModifier.command),
                target: TargetChord(windowsVk: WinVk.b, winModifiers: WinMod.control),
                enabled: false).get()
            switch model.setCustomEnabled(id: dupe.id, enabled: true) {
            case .success:
                try harness.fail("enable conflicting rule must fail")
            case .failure(let error):
                harness.expectEqual(error, .duplicateSource(ruleID: custom.id))
                harness.expect(model.localizedMessage(for: error).contains("my copy"),
                               "message names the conflicting custom (user label)")
            }
            harness.expect(!model.customRules.first { $0.id == dupe.id }!.enabled,
                           "failed enable changes nothing")
            // Disabling never conflicts.
            switch model.setCustomEnabled(id: custom.id, enabled: false) {
            case .success:
                break
            case .failure(let error):
                _ = try harness.fail("disabling must succeed, got \(error)")
            }
        },

        .case("l10nShortcutKeysResolve") {
            var keys = [
                "shortcuts.section.title", "shortcuts.master.toggle",
                "shortcuts.interception.note", "shortcuts.builtIns.title",
                "shortcuts.customs.title", "shortcuts.customs.scopeNote",
                "shortcuts.customs.empty", "shortcuts.column.source",
                "shortcuts.column.target", "shortcuts.column.label",
                "shortcuts.custom.add", "shortcuts.custom.edit",
                "shortcuts.custom.delete", "shortcuts.custom.moveUp",
                "shortcuts.custom.moveDown", "shortcuts.custom.label",
                "shortcuts.custom.label.placeholder", "shortcuts.custom.source",
                "shortcuts.custom.source.capture", "shortcuts.custom.target",
                "shortcuts.custom.target.modifiers", "shortcuts.custom.enabled",
                "shortcuts.custom.save", "shortcuts.custom.cancel",
                "shortcuts.custom.title.add", "shortcuts.custom.title.edit",
                "shortcuts.validation.emptyTrigger",
                "shortcuts.validation.modifierOnlyTrigger",
                "shortcuts.validation.emptyTarget",
                "shortcuts.validation.unsupportedTarget",
                "shortcuts.validation.unsupportedSource",
                "shortcuts.validation.duplicateSource",
                "shortcuts.store.futureVersion", "shortcuts.store.corrupt",
            ]
            for id in ["copy", "cut", "paste", "selectAll", "undo", "redo", "find",
                       "save", "open", "newDoc", "print", "closeDoc", "quitApp",
                       "refresh", "newTab", "saveAs"] {
                keys.append("shortcuts.builtIn.\(id)")
            }
            for key in keys {
                harness.expect(L10n.t(key) != key, "L10n key must resolve: \(key)")
            }
            harness.expectEqual(L10n.t("shortcuts.master.toggle"),
                                "Adapt macOS shortcuts for Windows")
            harness.expectEqual(L10n.t("shortcuts.section.title"), "Keyboard Shortcuts")
            harness.expect(L10n.t("shortcuts.interception.note").contains("⌘Tab"),
                           "interception note discloses the OS limit")
        },

        .case("chordDisplayFormat") {
            harness.expectEqual(
                ChordDisplay.macSource(SourceChord(keyCode: MacKey.c,
                                                   macModifiers: MacModifier.command)),
                "⌘C")
            harness.expectEqual(
                ChordDisplay.macSource(SourceChord(keyCode: MacKey.s,
                                                   macModifiers: MacModifier.command | MacModifier.shift)),
                "⌘⇧S")
            harness.expectEqual(ChordDisplay.macSource(SourceChord(keyCode: MacKey.a,
                                                                   macModifiers: 0)),
                                "A")
            harness.expectEqual(
                ChordDisplay.winTarget(TargetChord(windowsVk: WinVk.s,
                                                   winModifiers: WinMod.control | WinMod.shift)),
                "Ctrl+Shift+S")
            harness.expectEqual(
                ChordDisplay.winTarget(TargetChord(windowsVk: WinVk.c,
                                                   winModifiers: WinMod.control)),
                "Ctrl+C")
            harness.expectEqual(
                ChordDisplay.winTarget(TargetChord(windowsVk: WinVk.f4,
                                                   winModifiers: WinMod.alt)),
                "Alt+F4")
            harness.expectEqual(
                ChordDisplay.winTarget(TargetChord(windowsVk: WinVk.a, winModifiers: 0)),
                "A")
        },

        .case("macKeyCodeTableMatchesSdKHeaders") {
            // Spot-check the physical codes verified against
            // Carbon HIToolbox Events.h (plan rule 2).
            harness.expectEqual(MacKey.a, 0x00)
            harness.expectEqual(MacKey.c, 0x08)
            harness.expectEqual(MacKey.x, 0x07)
            harness.expectEqual(MacKey.z, 0x06)
            harness.expectEqual(MacKey.s, 0x01)
            harness.expectEqual(MacKey.q, 0x0C)
            harness.expectEqual(MacKey.t, 0x11)
            harness.expectEqual(MacKey.f4, 0x76)
            harness.expectEqual(MacKey.command, 0x37)
            harness.expectEqual(MacKey.shift, 0x38)
            // Target VKs are the contract's Windows domain (VK 'C' = 67
            // matches the pinned valid_input_key_down vector).
            harness.expectEqual(WinVk.c, 0x43)
            harness.expectEqual(WinVk.f4, 0x73)
            harness.expectEqual(WinVk.menu, 0x12)
        },
    ]
}
