#include "../firmware/sloth_pet/pet_speech.h"
#include "../firmware/sloth_pet/pet_renderer.h"
#include "../firmware/sloth_pet/gestures.h"
#include <cassert>
#include <cstring>
#include <cstdio>

int main() {
  using namespace sloth;
  assert(wrapSpeech(nullptr).count == 0);
  assert(wrapSpeech(" \n \t").count == 0);
  auto wrapped = wrapSpeech("I have a five-year plan. It is mostly a very long lunch.");
  assert(!wrapped.truncated && wrapped.count == 2);
  assert(std::strcmp(wrapped.text[0], "I have a five-year plan. It is") == 0);
  char huge[600]; std::memset(huge, 'x', sizeof(huge)); huge[599] = 0;
  wrapped = wrapSpeech(huge);
  assert(wrapped.truncated && wrapped.count == kSpeechLines);
  assert(std::strcmp(wrapped.text[6] + 29, "...") == 0);
  PetSpeech speech;
  assert(speech.text(0) == nullptr);
  speech.show("Noted. Disregarded, but noted.", UINT32_MAX - 100);
  assert(speech.text(20) != nullptr);
  assert(speech.text(9900) == nullptr);
  speech.show(huge, 10);
  assert(speech.text(22009) != nullptr && speech.text(22010) == nullptr);
  speech.clear(); assert(!speech.text(11));
  assert(speechDurationMs(nullptr) == 0 && speechDurationMs("") == 0);

  const char* shortLine = "Noted.";
  const char* twoLines = "I have a five-year plan. It is mostly a very long lunch.";
  const auto empty = speechLayout(nullptr);
  assert(!empty.lines.count && !empty.x && !empty.y && !empty.width && !empty.height);
  assert(!speechLayout(" \n \t").height);
  const auto small = speechLayout(shortLine);
  assert(small.lines.count == 1 && small.width == 80 && small.height == 23);
  assert(small.x == 80 && small.y == 145);
  const auto medium = speechLayout(twoLines);
  assert(medium.lines.count == 2 && medium.height == 33 && medium.y == 135);
  assert(medium.width == 30*6-1+16);
  const auto largest = speechLayout(huge);
  assert(largest.lines.count == 7 && largest.height == 83 && largest.y == 85);
  assert(largest.width == 207 && largest.x == 16);

  uint16_t overlay[240*240];
  const char* texts[] = {nullptr,"", " \t\n", shortLine, twoLines, huge};
  for (const char* text : texts) {
    std::memset(overlay,0,sizeof(overlay));
    drawPetSpeech(overlay,text);
    const auto layout = speechLayout(text);
    for (int y = -1; y <= 240; ++y) for (int x = -1; x <= 240; ++x) {
      const bool body = hitTestPetSpeech(x,y,nullptr);
      const bool onBubble = x>=0 && x<240 && y>=0 && y<240 && overlay[y*240+x] != 0;
      assert(hitTestPetSpeech(x,y,text) == (body || onBubble));
      if (body || onBubble) {
        assert(y>=46 && y<168);
        assert(hitTestAction(x,y)==-1);
      }
      if (onBubble) assert(x>=layout.x && x<layout.x+layout.width && y>=layout.y-6 && y<168);
    }
    for (unsigned i=0;i<layout.lines.count;++i) {
      const int textWidth=static_cast<int>(std::strlen(layout.lines.text[i]))*6-1;
      assert(layout.x+8+textWidth <= layout.x+layout.width-8);
      assert(layout.y+7+static_cast<int>(i)*10+7 <= 168);
    }
  }
  assert(hitTestPetSpeech(120,99,nullptr));
  assert(hitTestPetSpeech(56,99,nullptr) && hitTestPetSpeech(184,99,nullptr));
  assert(hitTestPetSpeech(120,49,nullptr) && hitTestPetSpeech(120,149,nullptr));
  assert(!hitTestPetSpeech(55,99,nullptr) && !hitTestPetSpeech(120,150,nullptr));
  assert(!hitTestPetSpeech(20,48,shortLine));
  assert(!hitTestPetSpeech(40,160,shortLine)); // Short bubbles do not own an invisible wide card.
  assert(hitTestPetSpeech(90,158,shortLine) && !hitTestPetSpeech(90,158,nullptr));
  assert(!hitTestPetSpeech(INT32_MIN,INT32_MAX,huge));
  assert(!hitTestPetSpeech(INT32_MAX,INT32_MIN,huge));
  TouchTap tap;
  assert(tap.update(true, kPetTalkTarget, 120, 90, 0) == -1);
  assert(tap.update(false, -1, 120, 90, 100) == -1);
  assert(tap.update(false, -1, 120, 90, 160) == kPetTalkTarget);
  assert(tap.update(false, -1, 120, 90, 220) == -1); // One joke per release.
  tap.update(true, kPetTalkTarget, 120, 90, 300);
  tap.update(true, kPetTalkTarget, 150, 90, 350);
  tap.update(false, -1, 150, 90, 400);
  assert(tap.update(false, -1, 150, 90, 460) == -1); // Drag is not a new joke.

  Snapshot pet{}; pet.fullness = 40; pet.happiness = 60; pet.energy = 80;
  uint16_t guarded[240 * 240 + 2], plain[240 * 240];
  const char* jokes[] = {"Noted.", "My calendar is full. Mostly of evidence against optimism.", huge};
  for (const char* joke : jokes) for (unsigned animal = 0; animal < 3; ++animal) for (unsigned sleep = 0; sleep < 2; ++sleep) {
    Settings settings; settings.animal = static_cast<Animal>(animal); pet.sleeping = sleep;
    drawPet(plain, pet, 1, "HELLO", 777, 0, Hud(), settings);
    guarded[0] = 0x1234; guarded[240 * 240 + 1] = 0xabcd;
    drawPet(guarded + 1, pet, 1, "HELLO", 777, 0, Hud(), settings, joke);
    assert(guarded[0] == 0x1234 && guarded[240 * 240 + 1] == 0xabcd);
    assert(std::memcmp(plain, guarded + 1, 46 * 240 * sizeof(uint16_t)) == 0);
    assert(std::memcmp(plain + 173 * 240, guarded + 1 + 173 * 240, 16 * 240 * sizeof(uint16_t)) == 0);
    assert(std::memcmp(plain + 190 * 240, guarded + 1 + 190 * 240, 50 * 240 * sizeof(uint16_t)) == 0);
    std::memset(overlay,0,sizeof(overlay)); drawPetSpeech(overlay,joke);
    for (int i=46*240;i<173*240;++i)
      if (!overlay[i]) assert(plain[i]==guarded[1+i]); // Full-size pet stays exactly in place.
    // Direct overlay rendering is the same as the integrated speech frame.
    drawPetSpeech(plain,joke);
    assert(std::memcmp(plain,guarded+1,sizeof(plain)) == 0);
  }
  drawPetSpeech(nullptr, twoLines);
  puts("Speech: fitted layouts, exact bubble/pet hit union, no portrait shrink, timing, gestures, all species/naps and HUD/status/meter bounds passed");
}
