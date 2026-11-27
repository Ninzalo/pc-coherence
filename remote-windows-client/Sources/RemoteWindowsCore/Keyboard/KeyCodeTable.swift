import Foundation

/// macOS virtual key codes (kVK_*) — FR-08 8.1 requires matching on
/// PHYSICAL key codes, never on localized characters.
///
/// Every constant below was verified against
/// `Carbon.framework/HIToolbox.framework/Headers/Events.h` in the
/// macOS 27.2 Command Line Tools SDK (plan rule 2: named constants
/// citing the header, never magic numbers).
public enum MacKey {
    // MARK: ANSI layout-dependent keys (Events.h "ANSI keyboard" enum)

    public static let a: UInt16 = 0x00   // kVK_ANSI_A
    public static let s: UInt16 = 0x01   // kVK_ANSI_S
    public static let d: UInt16 = 0x02   // kVK_ANSI_D
    public static let f: UInt16 = 0x03   // kVK_ANSI_F
    public static let h: UInt16 = 0x04   // kVK_ANSI_H
    public static let g: UInt16 = 0x05   // kVK_ANSI_G
    public static let z: UInt16 = 0x06   // kVK_ANSI_Z
    public static let x: UInt16 = 0x07   // kVK_ANSI_X
    public static let c: UInt16 = 0x08   // kVK_ANSI_C
    public static let v: UInt16 = 0x09   // kVK_ANSI_V
    public static let b: UInt16 = 0x0B   // kVK_ANSI_B
    public static let q: UInt16 = 0x0C   // kVK_ANSI_Q
    public static let w: UInt16 = 0x0D   // kVK_ANSI_W
    public static let e: UInt16 = 0x0E   // kVK_ANSI_E
    public static let r: UInt16 = 0x0F   // kVK_ANSI_R
    public static let y: UInt16 = 0x10   // kVK_ANSI_Y
    public static let t: UInt16 = 0x11   // kVK_ANSI_T
    public static let one: UInt16 = 0x12   // kVK_ANSI_1
    public static let two: UInt16 = 0x13   // kVK_ANSI_2
    public static let three: UInt16 = 0x14 // kVK_ANSI_3
    public static let four: UInt16 = 0x15  // kVK_ANSI_4
    public static let six: UInt16 = 0x16   // kVK_ANSI_6
    public static let five: UInt16 = 0x17  // kVK_ANSI_5
    public static let equal: UInt16 = 0x18 // kVK_ANSI_Equal
    public static let nine: UInt16 = 0x19  // kVK_ANSI_9
    public static let seven: UInt16 = 0x1A // kVK_ANSI_7
    public static let minus: UInt16 = 0x1B // kVK_ANSI_Minus
    public static let eight: UInt16 = 0x1C // kVK_ANSI_8
    public static let zero: UInt16 = 0x1D  // kVK_ANSI_0
    public static let rightBracket: UInt16 = 0x1E // kVK_ANSI_RightBracket
    public static let o: UInt16 = 0x1F     // kVK_ANSI_O
    public static let u: UInt16 = 0x20     // kVK_ANSI_U
    public static let leftBracket: UInt16 = 0x21 // kVK_ANSI_LeftBracket
    public static let i: UInt16 = 0x22     // kVK_ANSI_I
    public static let p: UInt16 = 0x23     // kVK_ANSI_P
    public static let l: UInt16 = 0x25     // kVK_ANSI_L
    public static let j: UInt16 = 0x26     // kVK_ANSI_J
    public static let quote: UInt16 = 0x27 // kVK_ANSI_Quote
    public static let k: UInt16 = 0x28     // kVK_ANSI_K
    public static let semicolon: UInt16 = 0x29 // kVK_ANSI_Semicolon
    public static let backslash: UInt16 = 0x2A // kVK_ANSI_Backslash
    public static let comma: UInt16 = 0x2B   // kVK_ANSI_Comma
    public static let slash: UInt16 = 0x2C   // kVK_ANSI_Slash
    public static let n: UInt16 = 0x2D     // kVK_ANSI_N
    public static let m: UInt16 = 0x2E     // kVK_ANSI_M
    public static let period: UInt16 = 0x2F // kVK_ANSI_Period
    public static let grave: UInt16 = 0x32 // kVK_ANSI_Grave

    // MARK: Layout-independent keys (Events.h "independent of layout" enum)

    public static let returnKey: UInt16 = 0x24   // kVK_Return
    public static let tab: UInt16 = 0x30         // kVK_Tab
    public static let space: UInt16 = 0x31       // kVK_Space
    public static let delete: UInt16 = 0x33      // kVK_Delete (Backspace)
    public static let escape: UInt16 = 0x35      // kVK_Escape
    public static let command: UInt16 = 0x37     // kVK_Command (left)
    public static let rightCommand: UInt16 = 0x36 // kVK_RightCommand
    public static let shift: UInt16 = 0x38       // kVK_Shift (left)
    public static let rightShift: UInt16 = 0x3C  // kVK_RightShift
    public static let option: UInt16 = 0x3A      // kVK_Option (left)
    public static let rightOption: UInt16 = 0x3D // kVK_RightOption
    public static let control: UInt16 = 0x3B     // kVK_Control (left)
    public static let rightControl: UInt16 = 0x3E // kVK_RightControl
    public static let forwardDelete: UInt16 = 0x75 // kVK_ForwardDelete
    public static let function: UInt16 = 0x3F  // kVK_Function
    public static let f5: UInt16 = 0x60        // kVK_F5
    public static let f6: UInt16 = 0x61        // kVK_F6
    public static let f7: UInt16 = 0x62        // kVK_F7
    public static let f3: UInt16 = 0x63        // kVK_F3
    public static let f8: UInt16 = 0x64        // kVK_F8
    public static let f9: UInt16 = 0x65        // kVK_F9
    public static let f11: UInt16 = 0x67       // kVK_F11
    public static let f13: UInt16 = 0x69       // kVK_F13
    public static let f16: UInt16 = 0x6A       // kVK_F16
    public static let f14: UInt16 = 0x6B       // kVK_F14
    public static let f10: UInt16 = 0x6D       // kVK_F10
    public static let f12: UInt16 = 0x6F       // kVK_F12
    public static let f15: UInt16 = 0x71       // kVK_F15
    public static let home: UInt16 = 0x73      // kVK_Home
    public static let pageUp: UInt16 = 0x74    // kVK_PageUp
    public static let f4: UInt16 = 0x76        // kVK_F4
    public static let end: UInt16 = 0x77       // kVK_End
    public static let f2: UInt16 = 0x78        // kVK_F2
    public static let pageDown: UInt16 = 0x79  // kVK_PageDown
    public static let f1: UInt16 = 0x7A        // kVK_F1
    public static let leftArrow: UInt16 = 0x7B // kVK_LeftArrow
    public static let rightArrow: UInt16 = 0x7C // kVK_RightArrow
    public static let downArrow: UInt16 = 0x7D // kVK_DownArrow
    public static let upArrow: UInt16 = 0x7E   // kVK_UpArrow

    /// Every macOS key code this Client understands as a source chord
    /// key. Unknown source keys are rejected by `ShortcutRuleValidator`.
    public static let knownKeyCodes: Set<UInt16> = [
        a, s, d, f, h, g, z, x, c, v, b, q, w, e, r, y, t,
        one, two, three, four, six, five, equal, nine, seven, minus, eight, zero,
        rightBracket, o, u, leftBracket, i, p, l, j, quote, k,
        semicolon, backslash, comma, slash, n, m, period, grave,
        returnKey, tab, space, delete, escape,
        command, rightCommand, shift, rightShift, option, rightOption,
        control, rightControl, forwardDelete, function,
        f5, f6, f7, f3, f8, f9, f11, f13, f16, f14, f10, f12, f15,
        home, pageUp, f4, end, f2, pageDown, f1,
        leftArrow, rightArrow, downArrow, upArrow,
    ]
}

/// Windows virtual-key codes for the TARGET side of an adaptation rule.
///
/// Standard Windows VK constants (WINUSER.H). Values stay inside the
/// pinned contract domain: `windows_vk` is 1...0xFF (see
/// `remote_windows.proto` `KeyInput` + the `vk_out_of_range` validation
/// rule) — the pinned valid vector uses 67 ('C').
public enum WinVk {
    // Letters (ASCII): 'A' 0x41 ... 'Z' 0x5A.
    public static let a: UInt32 = 0x41
    public static let b: UInt32 = 0x42
    public static let c: UInt32 = 0x43
    public static let d: UInt32 = 0x44
    public static let e: UInt32 = 0x45
    public static let f: UInt32 = 0x46
    public static let g: UInt32 = 0x47
    public static let h: UInt32 = 0x48
    public static let i: UInt32 = 0x49
    public static let j: UInt32 = 0x4A
    public static let k: UInt32 = 0x4B
    public static let l: UInt32 = 0x4C
    public static let m: UInt32 = 0x4D
    public static let n: UInt32 = 0x4E
    public static let o: UInt32 = 0x4F
    public static let p: UInt32 = 0x50
    public static let q: UInt32 = 0x51
    public static let r: UInt32 = 0x52
    public static let s: UInt32 = 0x53
    public static let t: UInt32 = 0x54
    public static let u: UInt32 = 0x55
    public static let v: UInt32 = 0x56
    public static let w: UInt32 = 0x57
    public static let x: UInt32 = 0x58
    public static let y: UInt32 = 0x59
    public static let z: UInt32 = 0x5A
    // Digits (ASCII): '0' 0x30 ... '9' 0x39.
    public static let zero: UInt32 = 0x30
    public static let one: UInt32 = 0x31
    public static let two: UInt32 = 0x32
    public static let three: UInt32 = 0x33
    public static let four: UInt32 = 0x34
    public static let five: UInt32 = 0x35
    public static let six: UInt32 = 0x36
    public static let seven: UInt32 = 0x37
    public static let eight: UInt32 = 0x38
    public static let nine: UInt32 = 0x39
    // Function keys VK_F1 0x70 ... VK_F12 0x7B.
    public static let f1: UInt32 = 0x70
    public static let f2: UInt32 = 0x71
    public static let f3: UInt32 = 0x72
    public static let f4: UInt32 = 0x73
    public static let f5: UInt32 = 0x74
    public static let f6: UInt32 = 0x75
    public static let f7: UInt32 = 0x76
    public static let f8: UInt32 = 0x77
    public static let f9: UInt32 = 0x78
    public static let f10: UInt32 = 0x79
    public static let f11: UInt32 = 0x7A
    public static let f12: UInt32 = 0x7B
    // Modifier keys (emitted as part of the ordered sequence only;
    // never valid as the main target key — see supportedTargetVk).
    public static let shift: UInt32 = 0x10     // VK_SHIFT
    public static let control: UInt32 = 0x11   // VK_CONTROL
    public static let menu: UInt32 = 0x12      // VK_MENU (Alt)
    public static let leftWin: UInt32 = 0x5B   // VK_LWIN
    public static let rightWin: UInt32 = 0x5C  // VK_RWIN
    // Special keys.
    public static let tab: UInt32 = 0x09       // VK_TAB
    public static let back: UInt32 = 0x08      // VK_BACK
    public static let returnKey: UInt32 = 0x0D // VK_RETURN
    public static let escape: UInt32 = 0x1B    // VK_ESCAPE
    public static let space: UInt32 = 0x20     // VK_SPACE
    public static let prior: UInt32 = 0x21     // VK_PRIOR (Page Up)
    public static let next: UInt32 = 0x22      // VK_NEXT (Page Down)
    public static let end: UInt32 = 0x23       // VK_END
    public static let home: UInt32 = 0x24      // VK_HOME
    public static let left: UInt32 = 0x25      // VK_LEFT
    public static let up: UInt32 = 0x26        // VK_UP
    public static let right: UInt32 = 0x27     // VK_RIGHT
    public static let down: UInt32 = 0x28      // VK_DOWN
    public static let delete: UInt32 = 0x2E    // VK_DELETE
    // US-layout punctuation (VK_OEM_*).
    public static let oemSemicolon: UInt32 = 0xBA // VK_OEM_1
    public static let oemComma: UInt32 = 0xAB     // VK_OEM_COMMA
    public static let oemPeriod: UInt32 = 0xBE    // VK_OEM_2
    public static let oemMinus: UInt32 = 0xBD     // VK_OEM_MINUS
    public static let oemPlus: UInt32 = 0xBB      // VK_OEM_PLUS
    public static let oemOpenBracket: UInt32 = 0xDB // VK_OEM_4
    public static let oemBackslash: UInt32 = 0xDC // VK_OEM_5
    public static let oemCloseBracket: UInt32 = 0xDD // VK_OEM_6
    public static let oemQuestion: UInt32 = 0xBF  // VK_OEM_7
    public static let oemTilde: UInt32 = 0xC0     // VK_OEM_3
    public static let oemQuotes: UInt32 = 0xDE    // VK_OEM_102

    /// Every Windows VK this Client supports as the MAIN target key of a
    /// rule. Modifier VKs are intentionally excluded: they are expressed
    /// through `winModifiers` (WIN_MOD bits), not as the main key.
    public static let supportedTargetVk: Set<UInt32> = [
        a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z,
        zero, one, two, three, four, five, six, seven, eight, nine,
        f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
        tab, back, returnKey, escape, space,
        prior, next, end, home, left, up, right, down, delete,
        oemSemicolon, oemComma, oemPeriod, oemMinus, oemPlus,
        oemOpenBracket, oemBackslash, oemCloseBracket, oemQuestion,
        oemTilde, oemQuotes,
    ]

    /// Human-readable name for a supported VK (settings UI target picker,
    /// validation messages). Machine identifiers, never localized.
    public static func name(_ vk: UInt32) -> String {
        switch vk {
        case a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z:
            return String(UnicodeScalar(Int(vk - 0x41) + 65)!)
        case zero, one, two, three, four, five, six, seven, eight, nine:
            return String(UnicodeScalar(Int(vk - 0x30) + 48)!)
        case f1: return "F1"
        case f2: return "F2"
        case f3: return "F3"
        case f4: return "F4"
        case f5: return "F5"
        case f6: return "F6"
        case f7: return "F7"
        case f8: return "F8"
        case f9: return "F9"
        case f10: return "F10"
        case f11: return "F11"
        case f12: return "F12"
        case tab: return "Tab"
        case back: return "Backspace"
        case returnKey: return "Return"
        case escape: return "Esc"
        case space: return "Space"
        case prior: return "Page Up"
        case next: return "Page Down"
        case end: return "End"
        case home: return "Home"
        case left: return "Left"
        case up: return "Up"
        case right: return "Right"
        case down: return "Down"
        case delete: return "Delete"
        case oemSemicolon: return ";"
        case oemComma: return ","
        case oemPeriod: return "."
        case oemMinus: return "-"
        case oemPlus: return "="
        case oemOpenBracket: return "["
        case oemBackslash: return "\\"
        case oemCloseBracket: return "]"
        case oemQuestion: return "/"
        case oemTilde: return "`"
        case oemQuotes: return "'"
        default:
            return String(format: "0x%02X", vk)
        }
    }
}

/// Windows modifier bits — the exact constants from the pinned contract
/// (`remote_windows.proto`, `KeyInput` comments: WIN_MOD_SHIFT 0x1,
/// WIN_MOD_CONTROL 0x2, WIN_MOD_ALT 0x4, WIN_MOD_WIN 0x8; `modifier_bits`
/// must stay within 0...0xF per the `invalid_modifiers` validation rule).
public enum WinMod {
    public static let shift: UInt32 = 0x1
    public static let control: UInt32 = 0x2
    public static let alt: UInt32 = 0x4
    public static let win: UInt32 = 0x8
    public static let all: UInt32 = shift | control | alt | win
}

/// macOS modifier mask bits used by source chords. Raw values match
/// `NSEvent.ModifierFlags` (AppKit; the Keyboard core itself stays
/// Foundation-only so the test target can use it): shift 1<<17,
/// control 1<<18, option 1<<19, command 1<<20. Callers normalize with
/// `normalize(macModifiers:)` before matching.
public enum MacModifier {
    public static let shift: UInt32 = 0x20000
    public static let control: UInt32 = 0x40000
    public static let option: UInt32 = 0x80000
    public static let command: UInt32 = 0x100000
    /// The only bits that matter for shortcut matching; everything else
    /// (numeric pad, function, device dependent, caps lock...) is ignored.
    public static let all: UInt32 = shift | control | option | command

    public static func normalize(_ flags: UInt32) -> UInt32 { flags & all }
}

/// macOS → Windows baseline key mapping: the physical-key table the
/// future input pipeline uses for UNMATCHED typing (FR-08 8.1: no rule
/// match → baseline key/text translation path). Deterministic and
/// unit-testable; this task does not wire it into any event stream.
public enum KeyCodeTable {
    /// Baseline translation of a macOS physical key to a Windows VK.
    /// Returns nil for keys with no Windows counterpart (modifier keys,
    /// Fn, dead keys) — the caller leaves those to the text path.
    public static func windowsVk(for macKeyCode: UInt16) -> UInt32? {
        switch macKeyCode {
        case MacKey.a: return WinVk.a
        case MacKey.b: return WinVk.b
        case MacKey.c: return WinVk.c
        case MacKey.d: return WinVk.d
        case MacKey.e: return WinVk.e
        case MacKey.f: return WinVk.f
        case MacKey.g: return WinVk.g
        case MacKey.h: return WinVk.h
        case MacKey.i: return WinVk.i
        case MacKey.j: return WinVk.j
        case MacKey.k: return WinVk.k
        case MacKey.l: return WinVk.l
        case MacKey.m: return WinVk.m
        case MacKey.n: return WinVk.n
        case MacKey.o: return WinVk.o
        case MacKey.p: return WinVk.p
        case MacKey.q: return WinVk.q
        case MacKey.r: return WinVk.r
        case MacKey.s: return WinVk.s
        case MacKey.t: return WinVk.t
        case MacKey.u: return WinVk.u
        case MacKey.v: return WinVk.v
        case MacKey.w: return WinVk.w
        case MacKey.x: return WinVk.x
        case MacKey.y: return WinVk.y
        case MacKey.z: return WinVk.z
        case MacKey.zero: return WinVk.zero
        case MacKey.one: return WinVk.one
        case MacKey.two: return WinVk.two
        case MacKey.three: return WinVk.three
        case MacKey.four: return WinVk.four
        case MacKey.five: return WinVk.five
        case MacKey.six: return WinVk.six
        case MacKey.seven: return WinVk.seven
        case MacKey.eight: return WinVk.eight
        case MacKey.nine: return WinVk.nine
        case MacKey.f1: return WinVk.f1
        case MacKey.f2: return WinVk.f2
        case MacKey.f3: return WinVk.f3
        case MacKey.f4: return WinVk.f4
        case MacKey.f5: return WinVk.f5
        case MacKey.f6: return WinVk.f6
        case MacKey.f7: return WinVk.f7
        case MacKey.f8: return WinVk.f8
        case MacKey.f9: return WinVk.f9
        case MacKey.f10: return WinVk.f10
        case MacKey.f11: return WinVk.f11
        case MacKey.f12: return WinVk.f12
        case MacKey.tab: return WinVk.tab
        case MacKey.delete: return WinVk.back
        case MacKey.forwardDelete: return WinVk.delete
        case MacKey.returnKey: return WinVk.returnKey
        case MacKey.escape: return WinVk.escape
        case MacKey.space: return WinVk.space
        case MacKey.pageUp: return WinVk.prior
        case MacKey.pageDown: return WinVk.next
        case MacKey.end: return WinVk.end
        case MacKey.home: return WinVk.home
        case MacKey.leftArrow: return WinVk.left
        case MacKey.upArrow: return WinVk.up
        case MacKey.rightArrow: return WinVk.right
        case MacKey.downArrow: return WinVk.down
        case MacKey.semicolon: return WinVk.oemSemicolon
        case MacKey.comma: return WinVk.oemComma
        case MacKey.slash: return WinVk.oemQuestion
        case MacKey.minus: return WinVk.oemMinus
        case MacKey.equal: return WinVk.oemPlus
        case MacKey.leftBracket: return WinVk.oemOpenBracket
        case MacKey.backslash: return WinVk.oemBackslash
        case MacKey.rightBracket: return WinVk.oemCloseBracket
        case MacKey.grave: return WinVk.oemTilde
        case MacKey.quote: return WinVk.oemQuotes
        case MacKey.period: return WinVk.oemPeriod
        default:
            return nil // modifier keys, Fn, untranslatable
        }
    }
}
