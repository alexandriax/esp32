#include "game_audio.h"

namespace sloth {
#if __cplusplus < 201703L
constexpr unsigned GameAudio::kSampleRate;
constexpr int GameAudio::kMaxAmplitude;
#endif
namespace {
// Original four-bar miniature melodies, one eighth note per entry. Zero is a rest.
// Pong leaves room around its motif; Tetris climbs; Leaf Sweep skips in thirds.
const uint8_t kMelodies[3][32] = {
  {64, 0, 67, 69, 67, 0, 62, 64, 60, 64, 0, 67, 69, 67, 64, 0,
   65, 69, 67, 0, 64, 62, 60, 0, 62, 65, 64, 62, 60, 0, 67, 0},
  {69, 72, 71, 67, 69, 0, 64, 67, 72, 74, 72, 71, 67, 69, 0, 64,
   65, 69, 72, 0, 74, 72, 69, 65, 67, 71, 69, 64, 69, 0, 64, 0},
  {67, 71, 74, 0, 72, 69, 67, 0, 64, 67, 71, 69, 74, 0, 72, 0,
   69, 72, 76, 74, 71, 0, 67, 69, 72, 69, 66, 0, 67, 74, 71, 0}
};
const uint8_t kBass[3][8] = {{48, 55, 48, 57, 53, 48, 55, 48},
                            {45, 52, 48, 52, 53, 50, 52, 45},
                            {43, 48, 52, 50, 45, 43, 50, 43}};
const uint16_t kNoteDuration[3] = {4000, 3200, 3600};
// Equal-tempered pitch table, MIDI 36..84, rounded to the nearest hertz.
const uint16_t kHz[] = {
  65, 69, 73, 78, 82, 87, 92, 98, 104, 110, 117, 123,
  131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
  262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494,
  523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988, 1047
};
struct Cue {
  uint8_t priority;
  uint8_t notes[3];
  uint16_t samples[3];
};
const Cue kCues[] = {
  {1, {60, 0, 0}, {480, 0, 0}},       // Move: short woody tick.
  {1, {67, 72, 0}, {400, 560, 0}},     // Rotate: lifted double note.
  {2, {60, 67, 0}, {800, 1200, 0}},    // Serve.
  {1, {76, 0, 0}, {640, 0, 0}},       // Paddle.
  {1, {64, 0, 0}, {480, 0, 0}},       // Wall, lower than the paddle.
  {3, {67, 72, 76}, {1280, 1280, 2240}}, // Point.
  {2, {48, 0, 0}, {1120, 0, 0}},      // Lock.
  {3, {72, 76, 81}, {960, 1280, 2400}}, // Line clear.
  {4, {64, 60, 57}, {2240, 2240, 3840}}, // Game over: a soft descending cadence.
  {2, {79, 84, 0}, {480, 960, 0}},    // Leaf: light rising shimmer.
  {3, {43, 42, 40}, {400, 400, 1600}} // Can: low, hollow descending clink.
};

uint32_t increment(uint8_t midi) {
  return midi >= 36 && midi <= 84 ?
      static_cast<uint32_t>((static_cast<uint64_t>(kHz[midi - 36]) << 32) /
                            GameAudio::kSampleRate) : 0;
}

int32_t triangle(uint32_t phase) {
  const int32_t half = static_cast<int32_t>(phase >> 16);
  return half < 32768 ? half * 2 - 32768 : 98303 - half * 2;
}

// Attack and release both end at zero, avoiding sudden steps at note boundaries.
int32_t voice(uint32_t phase, uint32_t elapsed, uint32_t duration, int32_t amplitude) {
  if (elapsed >= duration) return 0;
  const uint32_t remaining = duration - 1 - elapsed;
  if (elapsed < 64) amplitude = amplitude * static_cast<int32_t>(elapsed) / 64;
  if (remaining < 256) amplitude = amplitude * static_cast<int32_t>(remaining) / 256;
  return triangle(phase) * amplitude / 32768;
}
}  // namespace

GameAudio::GameAudio() : game_(0), music_(false), effects_(false) { reset(); }

void GameAudio::configure(unsigned game, bool music, bool effects) {
  game = game < 3 ? game : 0;
  if (game_ != game) { game_ = game; reset(); }
  music_ = music;
  effects_ = effects;
  if (!effects_) { effectRemaining_ = 0; effectPriority_ = 0; }
}

void GameAudio::reset() {
  note_ = effectPriority_ = effectSegment_ = 0;
  effect_ = GameSound::Move;
  melodyPhase_ = bassPhase_ = effectPhase_ = 0;
  noteSample_ = effectSample_ = effectRemaining_ = 0;
  effectIncrement_ = 0;
  startNote();
}

void GameAudio::startNote() {
  melodyIncrement_ = increment(kMelodies[game_][note_]);
  bassIncrement_ = increment(kBass[game_][note_ / 4]);
  melodyPhase_ = 0;
  if (note_ % 4 == 0) bassPhase_ = 0;
}

void GameAudio::trigger(GameSound sound) {
  const unsigned index = static_cast<unsigned>(sound);
  if (!effects_ || index >= sizeof(kCues) / sizeof(kCues[0])) return;
  const Cue& cue = kCues[index];
  if (effectRemaining_ && cue.priority < effectPriority_) return;
  effect_ = sound;
  effectPriority_ = cue.priority;
  effectSegment_ = 0;
  startEffectSegment();
}

void GameAudio::startEffectSegment() {
  const Cue& cue = kCues[static_cast<unsigned>(effect_)];
  effectRemaining_ = cue.samples[effectSegment_];
  effectSample_ = effectPhase_ = 0;
  effectIncrement_ = increment(cue.notes[effectSegment_]);
}

void GameAudio::render(int16_t* samples, size_t count) {
  if (!samples) return;
  const uint32_t noteDuration = kNoteDuration[game_];
  for (size_t i = 0; i < count; ++i) {
    int32_t mixed = 0;
    if (music_) {
      if (melodyIncrement_)
        mixed += voice(melodyPhase_, noteSample_, noteDuration * 7 / 8, 1350);
      const uint32_t bassSample = (note_ % 4) * noteDuration + noteSample_;
      mixed += voice(bassPhase_, bassSample, noteDuration * 4, 550);
      melodyPhase_ += melodyIncrement_;
      bassPhase_ += bassIncrement_;
      if (++noteSample_ == noteDuration) {
        noteSample_ = 0;
        note_ = (note_ + 1) % 32;
        startNote();
      }
    }
    if (effects_ && effectRemaining_) {
      const Cue& cue = kCues[static_cast<unsigned>(effect_)];
      const uint32_t duration = cue.samples[effectSegment_];
      if (effect_ == GameSound::CanHit) {
        // A quiet fifth harmonic gives the can a hollow metallic color, while
        // keeping both oscillators inside the same gentle attack/release.
        mixed += voice(effectPhase_, effectSample_, duration, 1600);
        mixed += voice(effectPhase_ * 5u, effectSample_, duration, 600);
      } else {
        mixed += voice(effectPhase_, effectSample_, duration,
                       effect_ == GameSound::LeafCollect ? 2000 : 2800);
      }
      effectPhase_ += effectIncrement_;
      ++effectSample_;
      if (--effectRemaining_ == 0) {
        if (effectSegment_ < 2 && cue.samples[effectSegment_ + 1]) {
          ++effectSegment_;
          startEffectSegment();
        } else {
          effectPriority_ = 0;
        }
      }
    }
    if (mixed > kMaxAmplitude) mixed = kMaxAmplitude;
    if (mixed < -kMaxAmplitude) mixed = -kMaxAmplitude;
    samples[i] = static_cast<int16_t>(mixed);
  }
}

}  // namespace sloth
