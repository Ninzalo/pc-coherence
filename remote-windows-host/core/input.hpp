
#ifndef RWH_CORE_INPUT_HPP
#define RWH_CORE_INPUT_HPP

// FR-07 / Stage 4 (portable half): normalized key representation,
// macOS -> Windows translation, and stuck-key tracking.
// The macOS Client sends these; the Host re-sends them as SendInput on
// Windows (win32/InputController, Windows-only).

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace remotewindows::core {

// WIN_MOD_* constants mirror the wire modifier_bits (PROTOCOL §5.1).
inline constexpr uint32_t kWinModShift = 0x1;
inline constexpr uint32_t kWinModControl = 0x2;
inline constexpr uint32_t kWinModAlt = 0x4;
inline constexpr uint32_t kWinModWin = 0x8;

// macOS virtual keycodes (kVK_ANSI_* / kVK_* from EventKeywords.h).
enum MacKey : uint16_t {
  kMacEsc = 53, kMacTab = 48, kMacSpace = 49, kMacDelete = 51,
  kMacA = 0, kMacB = 11, kMacC = 8, kMacD = 2, kMacE = 14, kMacF = 3,
  kMacG = 5, kMacH = 4, kMacI = 34, kMacJ = 38, kMacK = 40, kMacL = 37,
  kMacM = 46, kMacN = 45, kMacO = 31, kMacP = 35, kMacQ = 12, kMacR = 15,
  kMacS = 1, kMacT = 17, kMacU = 32, kMacV = 9, kMacW = 13, kMacX = 7,
  kMacY = 16, kMacZ = 6,
  kMacD0 = 29, kMacD1 = 18, kMacD2 = 19, kMacD3 = 20, kMacD4 = 21,
  kMacD5 = 23, kMacD6 = 22, kMacD7 = 26, kMacD8 = 28, kMacD9 = 25,
  kMacComma = 43, kMacPeriod = 47, kMacSlash = 44, kMacBackslash = 42,
  kMacSemicolon = 41, kMacQuote = 39, kMacBracketL = 33, kMacBracketR = 30,
  kMacGrave = 50, kMacEqual = 24, kMacMinus = 27,
  kMacF1 = 122, kMacF2 = 120, kMacF3 = 99, kMacF4 = 118, kMacF5 = 96,
  kMacF6 = 97, kMacF7 = 98, kMacF8 = 100, kMacF9 = 101, kMacF10 = 109,
  kMacF11 = 103, kMacF12 = 111,
  kMacLeft = 123, kMacRight = 124, kMacDown = 125, kMacUp = 126,
  kMacReturn = 36, kMacEnterNumpad = 76, kMacHome = 115, kMacEnd = 119,
  kMacPageUp = 116, kMacPageDown = 121, kMacInsert = 104,
  kMacShiftL = 56, kMacShiftR = 60, kMacCapsLock = 57, kMacAltL = 58,
  kMacAltR = 61, kMacControlL = 59, kMacControlR = 62,
  kMacCommandL = 55, kMacCommandR = 54,
};

// Windows virtual-key codes (VK_*).
enum WinVk : uint16_t {
  kVkBack = 0x08, kVkTab = 0x09, kVkReturn = 0x0D, kVkShift = 0x10,
  kVkControl = 0x11, kVkMenu = 0x12, kVkPause = 0x13, kVkCapital = 0x14,
  kVkEsc = 0x1B, kVkSpace = 0x20, kVkPrior = 0x21, kVkNext = 0x22,
  kVkEnd = 0x23, kVkHome = 0x24, kVkLeft = 0x25, kVkUp = 0x26,
  kVkRight = 0x27, kVkDown = 0x28, kVkInsert = 0x2D, kVkDelete = 0x2E,
  kVkD0 = 0x30, kVkD1 = 0x31, kVkD2 = 0x32, kVkD3 = 0x33, kVkD4 = 0x34,
  kVkD5 = 0x35, kVkD6 = 0x36, kVkD7 = 0x37, kVkD8 = 0x38, kVkD9 = 0x39,
  kVkA = 0x41, kVkB = 0x42, kVkC = 0x43, kVkD = 0x44, kVkE = 0x45,
  kVkF = 0x46, kVkG = 0x47, kVkH = 0x48, kVkI = 0x49, kVkJ = 0x4A,
  kVkK = 0x4B, kVkL = 0x4C, kVkM = 0x4D, kVkN = 0x4E, kVkO = 0x4F,
  kVkP = 0x50, kVkQ = 0x51, kVkR = 0x52, kVkS = 0x53, kVkT = 0x54,
  kVkU = 0x55, kVkV = 0x56, kVkW = 0x57, kVkX = 0x58, kVkY = 0x59,
  kVkZ = 0x5A,
  kVkLWin = 0x5B, kVkRWin = 0x5C, kVkApps = 0x5D,
  kVkNumpad0 = 0x60, kVkNumpad9 = 0x69, kVkMultiply = 0x6A,
  kVkAdd = 0x6B, kVkSubtract = 0x6D, kVkDivide = 0x6F,
  kVkF1 = 0x70, kVkF2 = 0x71, kVkF3 = 0x72, kVkF4 = 0x73, kVkF5 = 0x74,
  kVkF6 = 0x75, kVkF7 = 0x76, kVkF8 = 0x77, kVkF9 = 0x78, kVkF10 = 0x79,
  kVkF11 = 0x7A, kVkF12 = 0x7B, kVkNumLock = 0x90,
  kVkLShift = 0xA0, kVkRShift = 0xA1, kVkLControl = 0xA2,
  kVkRControl = 0xA3, kVkLMenu = 0xA4, kVkRMenu = 0xA5,
};

// Translate one macOS key event to a Windows VK, or nullopt when the key
// has no Windows equivalent (e.g. some dead keys).
std::optional<uint16_t> macKeyToWinVk(uint16_t macKey);

// Modifier flags for a key event. `cmd/ctrl/alt/shift` are the pressed
// state AT the moment of the event (before this event's own key is added).
struct ModState {
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
  bool cmd = false;
  uint32_t winModifierBits() const {
    uint32_t b = 0;
    if (shift) b |= kWinModShift;
    if (ctrl) b |= kWinModControl;
    if (alt) b |= kWinModAlt;
    if (cmd) b |= kWinModWin;
    return b;
  }
};

// A translated Windows key event ready for the wire KeyInput.
struct WinKeyEvent {
  uint16_t vk = 0;
  uint32_t modifier_bits = 0;
  bool key_down = false;
  bool is_repeat = false;
};

// Translate a macOS key event (vk or modifier key) to a Windows event.
// Modifier keys produce events for their own VK with L/R variants.
std::optional<WinKeyEvent> translateMacKey(uint16_t macKey, bool keyDown,
                                           bool isRepeat,
                                           const ModState& mods);

// Tracks logical pressed state so a disconnect or focus loss can emit
// exactly the release events needed (FR-07: no stuck keys/buttons).
class KeyStateTracker {
 public:
  // Returns the ordered release events for anything currently pressed
  // (modifiers first, in reverse press order), then clears state.
  std::vector<WinKeyEvent> resetAll();
  // Feed translated events; updates state. key_up releases the key.
  void apply(const WinKeyEvent& ev);
  bool hasPressed() const;
  // Mouse buttons.
  void mouseDown(uint32_t button);
  void mouseUp(uint32_t button);
  std::vector<uint32_t> mousePressed() const;
  void resetMouse();
  size_t keyCount() const;
 private:
  std::vector<WinKeyEvent> pressed_;
  std::vector<uint32_t> mouse_pressed_;
};

// Conflict/duplicate check for shortcut rules (FR-08, Client-side logic
// kept here so both apps share one implementation and tests).
struct ShortcutRule {
  uint16_t mac_key = 0;
  bool shift = false, ctrl = false, alt = false, cmd = false;
  bool enabled = true;
  std::string id;
};

// Two rules conflict when they have identical source combinations.
bool rulesConflict(const ShortcutRule& a, const ShortcutRule& b);

}  // namespace remotewindows::core

#endif
