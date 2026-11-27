import Foundation

/// Builds ordered Windows key event sequences for matched targets
/// (FR-08 8.1: "emit the target key combination as ordered Windows
/// key-down/key-up events, including correct modifier-down and
/// modifier-up ordering").
///
/// Pure and table-driven: a matched target expands to
///   [modifier-downs in fixed order (Shift, Ctrl, Alt, Win),
///    key-down, key-up,
///    modifier-ups in REVERSE order]
/// each event carrying the modifier bits active AT THAT MOMENT (the
/// contract's `modifier_bits` is "at event time"). Example:
/// Ctrl+Shift+S → [Ctrl↓(0x2), Shift↓(0x3), S↓(0x3), S↑(0x3),
/// Shift↑(0x2), Ctrl↑(0x0)].
///
/// The events are `KeyInput` — the pinned contract's normalized key
/// type (`windows_vk` 1...0xFF, `modifier_bits` 0...0xF). This module
/// NEVER emits them on the wire; the future input pipeline (after
/// C-WINDOWS/H-INPUT) turns `.adapted` decisions into `input_event`
/// frames.
public enum KeyboardEventTranslator {
    /// Fixed, deterministic modifier-down order: Shift, Ctrl, Alt, Win.
    public static let modifierOrder: [(vk: UInt32, bit: UInt32)] = [
        (WinVk.shift, WinMod.shift),
        (WinVk.control, WinMod.control),
        (WinVk.menu, WinMod.alt),
        (WinVk.leftWin, WinMod.win),
    ]

    /// The full ordered down/up sequence for a matched target.
    public static func sequence(for target: TargetChord) -> [KeyInput] {
        var events: [KeyInput] = []
        var active: UInt32 = 0
        for (vk, bit) in modifierOrder where target.winModifiers & bit != 0 {
            active |= bit
            events.append(KeyInput(windowsVk: vk, modifierBits: active,
                                   keyDown: true, isRepeat: false))
        }
        events.append(KeyInput(windowsVk: target.windowsVk,
                               modifierBits: target.winModifiers,
                               keyDown: true, isRepeat: false))
        events.append(KeyInput(windowsVk: target.windowsVk,
                               modifierBits: target.winModifiers,
                               keyDown: false, isRepeat: false))
        for (vk, bit) in modifierOrder.reversed()
        where target.winModifiers & bit != 0 {
            active &= ~bit
            events.append(KeyInput(windowsVk: vk, modifierBits: active,
                                   keyDown: false, isRepeat: false))
        }
        return events
    }

    /// Baseline translation of an UNMATCHED macOS key event (FR-08 8.1:
    /// "forward ordinary keyboard input through the baseline key/text
    /// translation path so typing still works"). Modifier mapping:
    /// Shift→shift, Control→control, Option→alt, Command→win. Returns
    /// nil for keys without a Windows counterpart (modifier keys, Fn) —
    /// those are left to the text path; nothing is dropped.
    public static func baseline(_ event: KeyPressEvent) -> KeyInput? {
        guard let vk = KeyCodeTable.windowsVk(for: event.keyCode) else {
            return nil
        }
        var bits: UInt32 = 0
        if event.macModifiers & MacModifier.shift != 0 { bits |= WinMod.shift }
        if event.macModifiers & MacModifier.control != 0 { bits |= WinMod.control }
        if event.macModifiers & MacModifier.option != 0 { bits |= WinMod.alt }
        if event.macModifiers & MacModifier.command != 0 { bits |= WinMod.win }
        return KeyInput(windowsVk: vk, modifierBits: bits,
                        keyDown: event.isDown, isRepeat: event.isRepeat)
    }
}
