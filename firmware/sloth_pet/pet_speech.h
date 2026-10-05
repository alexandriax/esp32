#pragma once

#include <stdint.h>

namespace sloth {

enum { kPetTalkTarget = 4, kSpeechColumns = 32, kSpeechLines = 7 };
struct SpeechLines {
  char text[kSpeechLines][kSpeechColumns + 1] = {};
  uint8_t count = 0;
  bool truncated = false;
};

struct SpeechLayout {
  SpeechLines lines;
  int x = 0, y = 0, width = 0, height = 0;
};

// Words stay intact when possible; malformed/overlong input still stays inside
// the card. The bank compiler and tests require every shipped line to fit.
SpeechLines wrapSpeech(const char* text);
// Empty text has a zero layout. The bubble ends at y168; its tail points up.
SpeechLayout speechLayout(const char* text);
// The full-size pet ellipse is always tappable; text adds only its visible bubble.
bool hitTestPetSpeech(int x, int y, const char* speech);
uint32_t speechDurationMs(const char* text);

class PetSpeech {
 public:
  // Text must outlive the bubble; firmware supplies immutable flash strings.
  void show(const char* text, uint32_t now) {
    text_ = text; since_ = now; duration_ = speechDurationMs(text);
  }
  void clear() { text_ = nullptr; }
  const char* text(uint32_t now) const {
    return text_ && now - since_ < duration_ ? text_ : nullptr;
  }
 private:
  const char* text_ = nullptr;
  uint32_t since_ = 0, duration_ = 0;
};

// Draw over the full-size pet without clearing or replacing it with a portrait.
// The HUD/status through y45 and meters/controls from y173 are untouched.
void drawPetSpeech(uint16_t* pixels, const char* text);
}  // namespace sloth
