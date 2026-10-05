#pragma once

#include <stdint.h>

namespace sloth {

enum class LeafSweepPhase : uint8_t { Playing, GameOver };
enum class LeafSweepObjectType : uint8_t { Leaf, Can };
enum LeafSweepEvent : uint8_t {
  LeafSweepCollect = 1u << 0, LeafSweepCanHit = 1u << 1,
  LeafSweepRound = 1u << 2, LeafSweepGameOver = 1u << 3
};

struct LeafSweepObject {
  bool active = false;
  LeafSweepObjectType type = LeafSweepObjectType::Leaf;
  int16_t x = 0, y = 0;
  uint16_t ageMs = 0, respawnMs = 0;
  uint8_t variant = 0;
};

struct LeafSweepSnapshot {
  LeafSweepPhase phase = LeafSweepPhase::Playing;
  uint32_t score = 0, leafCount = 0, elapsedMs = 0;
  uint16_t multiplier = 300;  // Hundredths: 300 = 3.00x, display minimum 0.01x.
  uint8_t energy = 0;
  LeafSweepObject objects[17];
  bool pawVisible = false;
  int16_t pawX = 120, pawY = 150;
};

// Fixed-size, hardware-independent touch game. The score measures whole-run
// efficiency: floor(leaves * 100 * 60000 / (active milliseconds + 20000)).
// Idling lowers score; pause/resume excludes paused time. Cans add 25 energy,
// energy cools at three units/second, and 100 energy is the sole ending condition.
class LeafSweepGame {
 public:
  static constexpr int kLeafSlots = 12;
  static constexpr int kCanSlots = 5;
  static constexpr int kObjectCount = kLeafSlots + kCanSlots;
  static constexpr int kFieldLeft = 14, kFieldRight = 226;
  static constexpr int kFieldTop = 82, kFieldBottom = 218;
  static constexpr int kLeafGrowthMs = 500;
  static constexpr int kCanWarningMs = 700;
  static constexpr int kCanLifetimeMs = 8700;  // Warning plus eight hazardous seconds.
  static constexpr int kStepMs = 20, kMaxCatchupMs = 200;

  LeafSweepGame();
  void reset(uint32_t now, uint32_t seed);
  void advance(uint32_t now);
  void resume(uint32_t now);
  void touch(bool down, int x, int y, uint32_t now);
  void clearTouch();
  const LeafSweepSnapshot& snapshot() const { return state_; }
  uint8_t takeEvents();

 private:
  LeafSweepSnapshot state_;
  uint32_t random_, lastMs_;
  uint16_t accumulatedMs_, coolingFraction_, canSpawnMs_;
  uint8_t events_;

  uint32_t randomBelow(uint32_t limit);
  bool spawn(unsigned index);
  void updateScore();
  void sweep(int fromX, int fromY, int toX, int toY);
  void step();
  void finish();
  // Exact collision/timing fixtures without a runtime mutation API.
  friend struct LeafSweepTestAccess;
};

void drawLeafSweep(uint16_t* pixels, const LeafSweepGame& game);

}  // namespace sloth
