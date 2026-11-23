
#include "core/input.hpp"

#include <algorithm>

namespace remotewindows::core {

std::optional<uint16_t> macKeyToWinVk(uint16_t macKey) {
  switch (macKey) {
    case kMacEsc: return kVkEsc;
    case kMacTab: return kVkTab;
    case kMacSpace: return kVkSpace;
    case kMacDelete: return kVkBack;
    case kMacReturn: return kVkReturn;
    case kMacEnterNumpad: return kVkReturn;
    case kMacHome: return kVkHome;
    case kMacEnd: return kVkEnd;
    case kMacPageUp: return kVkPrior;
    case kMacPageDown: return kVkNext;
    case kMacInsert: return kVkInsert;
    case kMacLeft: return kVkLeft;
    case kMacRight: return kVkRight;
    case kMacDown: return kVkDown;
    case kMacUp: return kVkUp;
    case kMacComma: return uint16_t(',');
    case kMacPeriod: return uint16_t('.');
    case kMacSlash: return uint16_t('/');
    case kMacBackslash: return uint16_t('\\');
    case kMacSemicolon: return uint16_t(';');
    case kMacQuote: return uint16_t('\'');
    case kMacBracketL: return uint16_t('[');
    case kMacBracketR: return uint16_t(']');
    case kMacGrave: return uint16_t('`');
    case kMacEqual: return uint16_t('=');
    case kMacMinus: return uint16_t('-');
    default: break;
  }
  // NOTE: macOS virtual keycodes are NOT contiguous across digits or
  // F-keys, so these must be explicit lookups — a range check would
  // misclassify unrelated keys (e.g. '=' = 24 sits between D1 and D0).
  static const std::unordered_map<uint16_t, uint16_t> digits = {
      {kMacD0, kVkD0}, {kMacD1, kVkD1}, {kMacD2, kVkD2}, {kMacD3, kVkD3},
      {kMacD4, kVkD4}, {kMacD5, kVkD5}, {kMacD6, kVkD6}, {kMacD7, kVkD7},
      {kMacD8, kVkD8}, {kMacD9, kVkD9}};
  auto dit = digits.find(macKey);
  if (dit != digits.end()) return dit->second;
  static const std::unordered_map<uint16_t, uint16_t> letters = {
      {kMacA, kVkA}, {kMacB, kVkB}, {kMacC, kVkC}, {kMacD, kVkD},
      {kMacE, kVkE}, {kMacF, kVkF}, {kMacG, kVkG}, {kMacH, kVkH},
      {kMacI, kVkI}, {kMacJ, kVkJ}, {kMacK, kVkK}, {kMacL, kVkL},
      {kMacM, kVkM}, {kMacN, kVkN}, {kMacO, kVkO}, {kMacP, kVkP},
      {kMacQ, kVkQ}, {kMacR, kVkR}, {kMacS, kVkS}, {kMacT, kVkT},
      {kMacU, kVkU}, {kMacV, kVkV}, {kMacW, kVkW}, {kMacX, kVkX},
      {kMacY, kVkY}, {kMacZ, kVkZ}};
  auto it = letters.find(macKey);
  if (it != letters.end()) return it->second;
  {
    // Same non-contiguity note as digits: lookup only, no range test.
    static const std::unordered_map<uint16_t, uint16_t> fn = {
        {kMacF1, kVkF1}, {kMacF2, kVkF2}, {kMacF3, kVkF3}, {kMacF4, kVkF4},
        {kMacF5, kVkF5}, {kMacF6, kVkF6}, {kMacF7, kVkF7}, {kMacF8, kVkF8},
        {kMacF9, kVkF9}, {kMacF10, kVkF10}, {kMacF11, kVkF11},
        {kMacF12, kVkF12}};
    auto fit = fn.find(macKey);
    if (fit != fn.end()) return fit->second;
  }
  return std::nullopt;
}

std::optional<WinKeyEvent> translateMacKey(uint16_t macKey, bool keyDown,
                                           bool isRepeat,
                                           const ModState& mods) {
  uint16_t vk = 0;
  switch (macKey) {
    case kMacShiftL: vk = kVkLShift; break;
    case kMacShiftR: vk = kVkRShift; break;
    case kMacControlL: vk = kVkLControl; break;
    case kMacControlR: return std::nullopt;  // R-ctrl maps to L on Win
    case kMacAltL: vk = kVkLMenu; break;
    case kMacAltR: return std::nullopt;
    case kMacCommandL: vk = kVkLWin; break;
    case kMacCommandR: vk = kVkRWin; break;
    case kMacCapsLock: vk = kVkCapital; break;
    default: {
      auto m = macKeyToWinVk(macKey);
      if (!m) return std::nullopt;
      vk = *m;
      break;
    }
  }
  WinKeyEvent ev;
  ev.vk = vk;
  ev.modifier_bits = mods.winModifierBits();
  ev.key_down = keyDown;
  ev.is_repeat = isRepeat;
  return ev;
}

std::vector<WinKeyEvent> KeyStateTracker::resetAll() {
  std::vector<WinKeyEvent> releases;
  // Release in reverse press order; modifier up-events follow key-ups.
  for (auto it = pressed_.rbegin(); it != pressed_.rend(); ++it) {
    WinKeyEvent ev = *it;
    ev.key_down = false;
    ev.is_repeat = false;
    releases.push_back(ev);
  }
  pressed_.clear();
  return releases;
}

void KeyStateTracker::apply(const WinKeyEvent& ev) {
  if (ev.key_down) {
    bool found = false;
    for (auto& p : pressed_)
      if (p.vk == ev.vk) { found = true; break; }
    if (!found) pressed_.push_back(ev);
  } else {
    auto it = std::find_if(pressed_.begin(), pressed_.end(),
                           [&](const WinKeyEvent& p) { return p.vk == ev.vk; });
    if (it != pressed_.end()) pressed_.erase(it);
  }
}

bool KeyStateTracker::hasPressed() const { return !pressed_.empty(); }

void KeyStateTracker::mouseDown(uint32_t button) {
  if (std::find(mouse_pressed_.begin(), mouse_pressed_.end(), button) ==
      mouse_pressed_.end())
    mouse_pressed_.push_back(button);
}

void KeyStateTracker::mouseUp(uint32_t button) {
  mouse_pressed_.erase(std::remove(mouse_pressed_.begin(),
                                   mouse_pressed_.end(), button),
                       mouse_pressed_.end());
}

std::vector<uint32_t> KeyStateTracker::mousePressed() const {
  return mouse_pressed_;
}

void KeyStateTracker::resetMouse() { mouse_pressed_.clear(); }

size_t KeyStateTracker::keyCount() const { return pressed_.size(); }

bool rulesConflict(const ShortcutRule& a, const ShortcutRule& b) {
  return a.mac_key == b.mac_key && a.shift == b.shift && a.ctrl == b.ctrl &&
         a.alt == b.alt && a.cmd == b.cmd;
}

}  // namespace remotewindows::core
