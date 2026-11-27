import Foundation

/// The macOS side of a shortcut rule: a PHYSICAL key code + modifier
/// mask (FR-08 8.1 — matching never uses localized characters).
///
/// `keyCode == nil` means "no key captured", which the validator
/// rejects (empty trigger or modifier-only trigger). Key codes are the
/// kVK_* constants from `KeyCodeTable.swift` (stable machine
/// identifiers, never localized).
public struct SourceChord: Codable, Equatable, Hashable {
    public var keyCode: UInt16?
    /// Normalized macOS modifier mask (`MacModifier` bits only).
    public var macModifiers: UInt32

    public init(keyCode: UInt16?, macModifiers: UInt32) {
        self.keyCode = keyCode
        self.macModifiers = MacModifier.normalize(macModifiers)
    }
}

/// The Windows side of a shortcut rule: a target VK + WIN_MOD bits,
/// both inside the pinned contract domain (`windows_vk` 1...0xFF,
/// `modifier_bits` 0...0xF — `remote_windows.proto` `KeyInput`).
public struct TargetChord: Codable, Equatable, Hashable {
    /// 0 = no key chosen (empty target — rejected by the validator).
    public var windowsVk: UInt32
    public var winModifiers: UInt32

    public init(windowsVk: UInt32, winModifiers: UInt32) {
        self.windowsVk = windowsVk
        self.winModifiers = winModifiers & WinMod.all
    }

    public var isEmpty: Bool { windowsVk == 0 }
}

/// Built-in catalog rules vs user-defined rules. They are stored and
/// versioned SEPARATELY so a future catalog revision can never clobber
/// user state (FR-08 8.3).
public enum ShortcutRuleKind: String, Codable {
    case builtIn
    case custom
}

/// FR-08 8.3 optional scope. MVP ships global scope only; per-application
/// scope (resolved through the Host's `app_id`, never localized titles)
/// is deferred — the settings UI says so.
public enum ShortcutScope: Codable, Equatable, Hashable {
    case allApplications
    case application(appID: String)
}

/// One shortcut adaptation rule (FR-08 8.4). Rule `id`s are stable,
/// non-localized identifiers: built-in IDs are fixed catalog strings
/// (`copy`, `cut`, ...), custom IDs are `custom-<uuid>` created once.
/// The visible name is a localized label — for built-ins via the L10n
/// key `shortcuts.builtIn.<id>`, for customs the user's text.
public struct ShortcutRule: Codable, Equatable, Hashable, Identifiable {
    public var id: String
    public var kind: ShortcutRuleKind
    public var source: SourceChord
    public var target: TargetChord
    /// Display label. Built-ins: the rule ID (localized through L10n at
    /// display time). Customs: user-editable text.
    public var label: String
    public var enabled: Bool
    public var scope: ShortcutScope

    public init(id: String, kind: ShortcutRuleKind, source: SourceChord,
                target: TargetChord, label: String, enabled: Bool,
                scope: ShortcutScope = .allApplications) {
        self.id = id
        self.kind = kind
        self.source = source
        self.target = target
        self.label = label
        self.enabled = enabled
        self.scope = scope
    }
}

/// One macOS key event as the `ShortcutAdaptationService` sees it.
/// Values mirror `NSEvent` (`keyCode`, `modifierFlags`, `type`,
/// `isARepeat`) but are plain data so the module stays Foundation-only
/// and unit-testable without AppKit.
public struct KeyPressEvent: Equatable {
    public var keyCode: UInt16
    /// Normalized macOS modifier mask at event time.
    public var macModifiers: UInt32
    public var isDown: Bool
    public var isRepeat: Bool

    public init(keyCode: UInt16, macModifiers: UInt32, isDown: Bool,
                isRepeat: Bool = false) {
        self.keyCode = keyCode
        self.macModifiers = MacModifier.normalize(macModifiers)
        self.isDown = isDown
        self.isRepeat = isRepeat
    }

    public static func down(_ keyCode: UInt16, _ mods: UInt32,
                            isRepeat: Bool = false) -> KeyPressEvent {
        KeyPressEvent(keyCode: keyCode, macModifiers: mods, isDown: true, isRepeat: isRepeat)
    }

    public static func up(_ keyCode: UInt16, _ mods: UInt32) -> KeyPressEvent {
        KeyPressEvent(keyCode: keyCode, macModifiers: mods, isDown: false, isRepeat: false)
    }
}

/// The decision the future input pipeline acts on (FR-08 8.4 seam).
///
/// `.adapted` carries the COMPLETE ordered Windows down/up sequence for
/// the matched target (modifier-downs before the key, key-up, then
/// modifier-ups in reverse — `KeyboardEventTranslator`).
///
/// `.passthrough` means no adaptation applies to this event (no rule
/// matched, master off, rule disabled, or an ordinary unmatched key
/// event): the pipeline forwards it through the baseline key/text
/// translation path (`KeyboardEventTranslator.baseline`). Nothing is
/// dropped.
///
/// `.consumed` (refinement of the packet's two-case sketch) means the
/// event is part of an in-flight adapted chord: its target-side
/// counterpart was already emitted inside the `.adapted` sequence, so
/// the pipeline must emit NOTHING for it. Without this case, releasing
/// the physical chord keys would forward stray Windows key-ups for keys
/// Windows never received (FR-07 input-state hygiene).
public enum TranslationDecision: Equatable {
    case adapted([KeyInput])
    case passthrough
    case consumed
}

/// Stable, machine-neutral display of chords for the settings UI.
///
/// Source side uses macOS modifier SYMBOLS (⌘⌥⌃⇧ — Unicode symbols, not
/// translatable text); the target side uses the plan's English word form
/// (`Ctrl+Shift+S`, FR-08 §6.1 "such as ⌘C → Ctrl+C"). Surrounding UI
/// copy (labels, columns, messages) goes through L10n keys; the chord
/// notation itself is a stable machine identifier, like the rule ID.
public enum ChordDisplay {
    public static func macSource(_ chord: SourceChord) -> String {
        var text = ""
        if chord.macModifiers & MacModifier.command != 0 { text += "⌘" }
        if chord.macModifiers & MacModifier.option != 0 { text += "⌥" }
        if chord.macModifiers & MacModifier.control != 0 { text += "⌃" }
        if chord.macModifiers & MacModifier.shift != 0 { text += "⇧" }
        if let code = chord.keyCode {
            text += macKeySymbol(code)
        }
        return text
    }

    public static func winTarget(_ chord: TargetChord) -> String {
        var parts: [String] = []
        if chord.winModifiers & WinMod.control != 0 { parts.append("Ctrl") }
        if chord.winModifiers & WinMod.alt != 0 { parts.append("Alt") }
        if chord.winModifiers & WinMod.shift != 0 { parts.append("Shift") }
        if chord.winModifiers & WinMod.win != 0 { parts.append("Win") }
        if !chord.isEmpty { parts.append(WinVk.name(chord.windowsVk)) }
        return parts.joined(separator: "+")
    }

    public static func macKeySymbol(_ keyCode: UInt16) -> String {
        switch keyCode {
        case MacKey.a: return "A"
        case MacKey.s: return "S"
        case MacKey.d: return "D"
        case MacKey.f: return "F"
        case MacKey.h: return "H"
        case MacKey.g: return "G"
        case MacKey.z: return "Z"
        case MacKey.x: return "X"
        case MacKey.c: return "C"
        case MacKey.v: return "V"
        case MacKey.b: return "B"
        case MacKey.q: return "Q"
        case MacKey.w: return "W"
        case MacKey.e: return "E"
        case MacKey.r: return "R"
        case MacKey.y: return "Y"
        case MacKey.t: return "T"
        case MacKey.one: return "1"
        case MacKey.two: return "2"
        case MacKey.three: return "3"
        case MacKey.four: return "4"
        case MacKey.six: return "6"
        case MacKey.five: return "5"
        case MacKey.equal: return "="
        case MacKey.nine: return "9"
        case MacKey.seven: return "7"
        case MacKey.minus: return "-"
        case MacKey.eight: return "8"
        case MacKey.zero: return "0"
        case MacKey.rightBracket: return "]"
        case MacKey.o: return "O"
        case MacKey.u: return "U"
        case MacKey.leftBracket: return "["
        case MacKey.i: return "I"
        case MacKey.p: return "P"
        case MacKey.l: return "L"
        case MacKey.j: return "J"
        case MacKey.quote: return "'"
        case MacKey.k: return "K"
        case MacKey.semicolon: return ";"
        case MacKey.backslash: return "\\"
        case MacKey.comma: return ","
        case MacKey.slash: return "/"
        case MacKey.n: return "N"
        case MacKey.m: return "M"
        case MacKey.period: return "."
        case MacKey.grave: return "`"
        case MacKey.returnKey: return "↩"
        case MacKey.tab: return "⇥"
        case MacKey.space: return "␣"
        case MacKey.delete: return "⌫"
        case MacKey.forwardDelete: return "⌦"
        case MacKey.escape: return "⎋"
        case MacKey.shift: return "⇧"
        case MacKey.rightShift: return "⇧"
        case MacKey.control: return "⌃"
        case MacKey.rightControl: return "⌃"
        case MacKey.option: return "⌥"
        case MacKey.rightOption: return "⌥"
        case MacKey.command: return "⌘"
        case MacKey.rightCommand: return "⌘"
        case MacKey.f1: return "F1"
        case MacKey.f2: return "F2"
        case MacKey.f3: return "F3"
        case MacKey.f4: return "F4"
        case MacKey.f5: return "F5"
        case MacKey.f6: return "F6"
        case MacKey.f7: return "F7"
        case MacKey.f8: return "F8"
        case MacKey.f9: return "F9"
        case MacKey.f10: return "F10"
        case MacKey.f11: return "F11"
        case MacKey.f12: return "F12"
        case MacKey.home: return "↖"
        case MacKey.pageUp: return "⇞"
        case MacKey.end: return "↘"
        case MacKey.pageDown: return "⇟"
        case MacKey.leftArrow: return "←"
        case MacKey.rightArrow: return "→"
        case MacKey.downArrow: return "↓"
        case MacKey.upArrow: return "↑"
        default:
            return String(format: "0x%02X", keyCode)
        }
    }
}
