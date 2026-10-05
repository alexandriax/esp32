#pragma once

#include <stdint.h>

namespace sloth {

enum class ForestToy : uint8_t {
  DewPond, AcornRoll, MushroomPop, FernBrush, FireflyJar, PineconeSpin,
  PebbleStack, MossSquish, LeafGlobe, VineSwing, Rainstick, ZenRake, Count
};

// Common visible state. All coordinates are logical 240 x 240 screen pixels;
// velocities are pixels/second, angles radians, spin radians/second.
// Per-toy meanings of radius / value:
// Pond: ring radius / remaining opacity (0..1).
// Acorns, pebbles: collision radius / contact squash (0..1).
// Mushrooms: cap radius / compression (0..1); angle is cap lean.
// Ferns: frond length / unfurl (0..1); angle is bend from upright.
// Fireflies: glow radius / glow (0..1).
// Pinecone: spinner radius / touch glow (0..1); angle/spin are rotation.
// Moss: tuft radius / compression (0..1); angle is lateral bend.
// Leaves: leaf radius / flutter phase (radians).
// Vine: bob radius / string length; body 0 hangs from (120, 76).
// Rainstick: seed radius / bounce glow (0..1); static pegs use
// forestRainPegX/Y() below so renderer and physics share exact coordinates.
// Rake: body count is zero; trail points contain persistent fading sand marks.
struct ForestBody {
  float x = 0, y = 0, vx = 0, vy = 0;
  float angle = 0, spin = 0, radius = 0, value = 0;
  uint8_t variant = 0;
  bool active = false;
};

struct ForestTrailPoint {
  float x = 0, y = 0, strength = 0;
};

struct ForestFidgetSnapshot {
  static constexpr unsigned kBodies = 24, kTrailPoints = 96;
  ForestToy toy = ForestToy::DewPond;
  ForestBody bodies[kBodies];
  ForestTrailPoint trail[kTrailPoints];
  uint8_t bodyCount = 0, trailCount = 0, trailHead = 0;
  // trailHead points to the next slot to overwrite. In chronological order,
  // read (trailHead + kTrailPoints - trailCount + i) % kTrailPoints.
  bool touching = false, motionAvailable = false;
  int16_t touchX = 120, touchY = 140;
  float tiltX = 0, tiltY = 0; // Screen-aligned gravity, clamped to +/-1.5 g.
  uint32_t elapsedMs = 0;
};

const char* forestToyName(ForestToy toy);
const char* forestToyHint(ForestToy toy);
constexpr unsigned kForestRainPegs = 15;
float forestRainPegX(unsigned index);
float forestRainPegY(unsigned index);

class ForestFidget {
 public:
  static constexpr int kFieldLeft = 16, kFieldRight = 224;
  static constexpr int kFieldTop = 68, kFieldBottom = 216;
  static constexpr int kStepMs = 20, kMaxCatchupMs = 200;
  ForestFidget();
  void reset(uint32_t now, uint32_t seed);
  void cycle(int direction, uint32_t now);
  void advance(uint32_t now);
  void resume(uint32_t now);
  void touch(bool down, int logicalX, int logicalY, uint32_t now);
  // Root supplies screen-right +x and screen-down +y gravity in g. z is
  // validated but not used for 2D motion. Nonfinite or >8g samples are ignored.
  void motion(float xg, float yg, float zg, uint32_t now);
  void clearTouch();
  const ForestFidgetSnapshot& snapshot() const { return state_; }

 private:
  ForestFidgetSnapshot state_;
  uint32_t random_ = 1, lastMs_ = 0, motionMs_ = 0;
  uint16_t accumulatedMs_ = 0;
  int8_t grabbed_ = -1;
  uint8_t cursor_ = 0;
  float pointerVx_ = 0, pointerVy_ = 0;
  uint32_t pointerMs_ = 0, movementMs_ = 0;
  float spinTouchAngle_ = 0;
  int16_t emittedX_ = 0, emittedY_ = 0;
  void beginToy(ForestToy toy, uint32_t now);
  uint32_t randomBelow(uint32_t limit);
  void step();
  void ring(float x, float y);
  void trail(float x, float y);
  void deform(float x, float y, float dx);
  int nearest(float x, float y, float reach) const;
  void confine(ForestBody& body, float bounce);
  void collide(float bounce);
};

void drawForestFidget(uint16_t* pixels, const ForestFidgetSnapshot& snapshot);

}  // namespace sloth
