import Foundation

/// The 16 built-in shortcut mappings with the plan's EXACT default
/// states (FR-08 8.2 — the plan table is normative; do not "improve"
/// the defaults).
///
/// Rule IDs are stable, non-localized identifiers (FR-08 8.4). Their
/// visible names come from L10n keys `shortcuts.builtIn.<id>`.
///
/// macOS triggers use kVK_* physical key codes (verified against the
/// CLT SDK `Events.h` header — see `KeyCodeTable.swift`); Windows
/// targets use the contract's VK domain (1...0xFF, WIN_MOD bits
/// 0...0xF — `remote_windows.proto` `KeyInput`).
public struct BuiltInShortcutCatalog {
    public let rules: [ShortcutRule]

    public init(rules: [ShortcutRule]) {
        self.rules = rules
    }

    public func rule(id: String) -> ShortcutRule? {
        rules.first { $0.id == id }
    }

    /// The normative 16-rule table (FR-08 8.2), in plan order:
    /// copy, cut, paste, selectAll, undo, redo, find, save, open,
    /// newDoc, print, closeDoc (enabled by default) then quitApp,
    /// refresh, newTab, saveAs (disabled by default — potentially
    /// destructive or application-specific per the plan's Notes column).
    public static let standard = BuiltInShortcutCatalog(rules: [
        rule(id: "copy",     macKey: MacKey.c, macMods: MacModifier.command,
             winVk: WinVk.c, winMods: WinMod.control, enabled: true),
        rule(id: "cut",      macKey: MacKey.x, macMods: MacModifier.command,
             winVk: WinVk.x, winMods: WinMod.control, enabled: true),
        rule(id: "paste",    macKey: MacKey.v, macMods: MacModifier.command,
             winVk: WinVk.v, winMods: WinMod.control, enabled: true),
        rule(id: "selectAll", macKey: MacKey.a, macMods: MacModifier.command,
             winVk: WinVk.a, winMods: WinMod.control, enabled: true),
        rule(id: "undo",     macKey: MacKey.z, macMods: MacModifier.command,
             winVk: WinVk.z, winMods: WinMod.control, enabled: true),
        rule(id: "redo",     macKey: MacKey.z, macMods: MacModifier.command | MacModifier.shift,
             winVk: WinVk.y, winMods: WinMod.control, enabled: true),
        rule(id: "find",     macKey: MacKey.f, macMods: MacModifier.command,
             winVk: WinVk.f, winMods: WinMod.control, enabled: true),
        rule(id: "save",     macKey: MacKey.s, macMods: MacModifier.command,
             winVk: WinVk.s, winMods: WinMod.control, enabled: true),
        rule(id: "open",     macKey: MacKey.o, macMods: MacModifier.command,
             winVk: WinVk.o, winMods: WinMod.control, enabled: true),
        rule(id: "newDoc",   macKey: MacKey.n, macMods: MacModifier.command,
             winVk: WinVk.n, winMods: WinMod.control, enabled: true),
        rule(id: "print",    macKey: MacKey.p, macMods: MacModifier.command,
             winVk: WinVk.p, winMods: WinMod.control, enabled: true),
        rule(id: "closeDoc", macKey: MacKey.w, macMods: MacModifier.command,
             winVk: WinVk.w, winMods: WinMod.control, enabled: true),
        rule(id: "quitApp",  macKey: MacKey.q, macMods: MacModifier.command,
             winVk: WinVk.f4, winMods: WinMod.alt, enabled: false),
        rule(id: "refresh",  macKey: MacKey.r, macMods: MacModifier.command,
             winVk: WinVk.r, winMods: WinMod.control, enabled: false),
        rule(id: "newTab",   macKey: MacKey.t, macMods: MacModifier.command,
             winVk: WinVk.t, winMods: WinMod.control, enabled: false),
        rule(id: "saveAs",   macKey: MacKey.s, macMods: MacModifier.command | MacModifier.shift,
             winVk: WinVk.s, winMods: WinMod.control | WinMod.shift, enabled: false),
    ])

    private static func rule(id: String, macKey: UInt16, macMods: UInt32,
                             winVk: UInt32, winMods: UInt32,
                             enabled: Bool) -> ShortcutRule {
        ShortcutRule(id: id, kind: .builtIn,
                     source: SourceChord(keyCode: macKey, macModifiers: macMods),
                     target: TargetChord(windowsVk: winVk, winModifiers: winMods),
                     label: id, enabled: enabled, scope: .allApplications)
    }
}
