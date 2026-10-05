#pragma once
#include <stdint.h>

namespace sloth {
// Local USB diagnostic packet: L<down>,<logical x>,<logical y>\n.
// Bounded parsing, independent of hardware, never forwards malformed packet
// contents as pet/control shortcuts. The caller routes completed samples through
// the same Tetris, Leaf Sweep, Forest Fidget, or overlay handler as the touchscreen.
class TouchCommand {
 public:
  enum class Result : uint8_t { Pending, Complete, Rejected };
  void begin(uint32_t now) {
    reading_ = true; bad_ = digit_ = false; field_ = 0; started_ = now;
    values_[0] = values_[1] = values_[2] = 0;
  }
  bool active() const { return reading_; }
  void cancel() { reading_ = false; }
  void discard() { if (reading_) bad_ = true; }
  // An expired line remains in discard mode until its newline. Otherwise a
  // delayed tail containing f/p/b could escape into unrelated control shortcuts.
  void expire(uint32_t now) { if (reading_ && now - started_ >= 1000) discard(); }
  Result feed(char ch) {
    if (!reading_) return Result::Rejected;
    if (ch == '\r') return Result::Pending;
    if (ch == '\n') {
      reading_ = false;
      return !bad_ && field_ == 2 && digit_ ? Result::Complete : Result::Rejected;
    }
    if (bad_) return Result::Pending;
    if (ch == ',') {
      if (!digit_ || field_ >= 2) bad_ = true;
      else { ++field_; digit_ = false; }
    } else if (ch >= '0' && ch <= '9') {
      digit_ = true;
      values_[field_] = static_cast<uint16_t>(values_[field_] * 10 + ch - '0');
      if (values_[field_] > (field_ ? 239 : 1)) bad_ = true;
    } else bad_ = true;
    return Result::Pending;
  }
  bool down() const { return values_[0] != 0; }
  int x() const { return values_[1]; }
  int y() const { return values_[2]; }
 private:
  bool reading_ = false, bad_ = false, digit_ = false;
  uint8_t field_ = 0;
  uint16_t values_[3] = {};
  uint32_t started_ = 0;
};
}  // namespace sloth
