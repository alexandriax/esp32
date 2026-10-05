#pragma once

#include <stddef.h>
#include <stdint.h>

namespace sloth {

enum class GameKind : uint8_t { Pong, Tetris, LeafSweep };
constexpr unsigned kGameCount = 3;
constexpr unsigned kScoreCount = 10;
constexpr unsigned kScoreNameLength = 10;
struct GameScore {
  char name[kScoreNameLength + 1] = {};
  uint32_t score = 0;
  uint16_t detail = 0;  // Pong: losing score. Tetris: lines. Leaf Sweep: leaves.
};
struct ScoreTable {
  uint8_t count = 0;
  GameScore entries[kScoreCount];
};
struct GameRecords {
  bool music[kGameCount] = {};
  bool effects[kGameCount] = {true, true, true};
  ScoreTable tables[kGameCount];
};

// Completed Pong wins rank by score, then smaller losing score (7-0 best).
// Tetris/Leaf Sweep rank by points, then lines/leaves. Equal scores keep precedence.
bool qualifies(const GameRecords& records, GameKind game, uint32_t score, uint16_t detail);
bool addGameScore(GameRecords& records, GameKind game, uint32_t score,
                  uint16_t detail, const char* name);
void formatGameScore(GameKind game, const GameScore& score, char* out, size_t size);
const char* gameTitle(GameKind game);

// Fixed, checksummed, explicit-endian format; no compiler padding. Includes
// v2: GAME, version2, flags (two bits/game), three counts, three reserved zeros,
// then three ten-entry tables (11 name bytes, LE32 score, LE16 detail), LE32 CRC.
// v1 (two games, eight-byte header) remains readable; Leaf Sweep gets defaults.
constexpr size_t kLegacyGameRecordSize = 352;
constexpr size_t kGameRecordSize = 526;
bool encodeGameRecords(const GameRecords& records, uint8_t* out, size_t size);
bool decodeGameRecords(const uint8_t* data, size_t size, GameRecords& records);

}  // namespace sloth
