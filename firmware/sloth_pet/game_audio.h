#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sloth {

enum class GameSound : uint8_t {
  Move, Rotate, Serve, Paddle, Wall, Point, Lock, LineClear, GameOver,
  LeafCollect, CanHit
};

// Allocation-free, deterministic 16 kHz mono synthesizer. Samples are signed
// PCM16 (little-endian when stored on ESP32-C6). No hardware or clock dependency.
// Calls must be serialized by the caller; render drives the musical clock.
class GameAudio {
 public:
  static constexpr unsigned kSampleRate = 16000;
  static constexpr int kMaxAmplitude = 6000;
  GameAudio();  // Music and effects are independently muted by default.
  void configure(unsigned game, bool music, bool effects);  // 0 Pong, 1 Tetris, 2 Leaf Sweep.
  void trigger(GameSound sound);  // Lower-priority effects cannot cut off a cue.
  void render(int16_t* samples, size_t count);
  void reset();  // Restart melody and clear effect; retain configuration.
  bool needsAudio() const { return music_ || (effects_ && effectRemaining_); }

 private:
  unsigned game_;
  bool music_, effects_;
  uint8_t note_, effectPriority_, effectSegment_;
  GameSound effect_;
  uint32_t melodyPhase_, bassPhase_, effectPhase_;
  uint32_t melodyIncrement_, bassIncrement_, effectIncrement_;
  uint32_t noteSample_, effectSample_, effectRemaining_;
  void startNote();
  void startEffectSegment();
};

}  // namespace sloth
